/*******************************************************************************
* File Name        : main.c
*
* Description      : This source file contains the main routine for 
*                    application running on CM55 CPU which handles 
*                    graphics display, touch input, voice assistant and
*                    environmental sensors.
* Related Document : See README.md
*
********************************************************************************
* (c) 2021-2026, Infineon Technologies AG, or an affiliate of Infineon
* Technologies AG. All rights reserved.
* This software, associated documentation and materials ("Software") is
* owned by Infineon Technologies AG or one of its affiliates ("Infineon")
* and is protected by and subject to worldwide patent protection, worldwide
* copyright laws, and international treaty provisions. Therefore, you may use
* this Software only as provided in the license agreement accompanying the
* software package from which you obtained this Software. If no license
* agreement applies, then any use, reproduction, modification, translation, or
* compilation of this Software is prohibited without the express written
* permission of Infineon.
*
* Disclaimer: UNLESS OTHERWISE EXPRESSLY AGREED WITH INFINEON, THIS SOFTWARE
* IS PROVIDED AS-IS, WITH NO WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
* INCLUDING, BUT NOT LIMITED TO, ALL WARRANTIES OF NON-INFRINGEMENT OF
* THIRD-PARTY RIGHTS AND IMPLIED WARRANTIES SUCH AS WARRANTIES OF FITNESS FOR A
* SPECIFIC USE/PURPOSE OR MERCHANTABILITY.
* Infineon reserves the right to make changes to the Software without notice.
* You are responsible for properly designing, programming, and testing the
* functionality and safety of your intended application of the Software, as
* well as complying with any legal requirements related to its use. Infineon
* does not guarantee that the Software will be free from intrusion, data theft
* or loss, or other breaches ("Security Breaches"), and Infineon shall have
* no liability arising out of any Security Breaches. Unless otherwise
* explicitly approved by Infineon, the Software may not be used in any
* application where a failure of the Product or any consequences of the use
* thereof can reasonably be expected to result in personal injury.
*******************************************************************************/

/*******************************************************************************
* Header Files
*******************************************************************************/
#include "cy_time.h"
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "queue.h"
#include "app_common.h"
#include "comm_manager.h"
#include "app_eeprom.h"
#include "app_sensor.h"

/* Application related includes */
#include "app_gfx_disp.h"
#include "app_voice_control.h"
#include "voice_assistant.h"
#include "thermostat_events.h"

/*******************************************************************************
* Macros
*******************************************************************************/
#define GFX_TASK_NAME                       ("CM55_GfxTask")
#define GFX_TASK_STACK_SIZE                 (configMINIMAL_STACK_SIZE * 20)
#define GFX_TASK_PRIORITY                   (3U)

#define VOICE_ASSISTANT_TASK_NAME           ("CM55_VoiceTask")
#define VOICE_ASSISTANT_TASK_STACK_SIZE     (configMINIMAL_STACK_SIZE * 2)
#define VOICE_ASSISTANT_TASK_PRIORITY       (4U)

#define I2C_CONTROLLER_IRQ_PRIORITY         (2UL)

/* Sized to absorb the largest expected burst of IPC messages arriving faster
 * than cm55_gfx_task's ~25ms loop drains them. Queued messages are drained in
 * order; if a burst still exceeds this depth the overflow is dropped and
 * counted in cm55_ipc_rx_drop_count. */
#define CM55_IPC_RX_QUEUE_LENGTH            (20U)

/* Enabling or disabling a MCWDT requires a wait time of upto 2 CLK_LF cycles
 * to come into effect. This wait time value will depend on the actual CLK_LF
 * frequency set by the BSP.
 */
#define LPTIMER_1_WAIT_TIME_USEC            (62U)

/* Define the LPTimer interrupt priority number. '1' implies highest priority.*/
#define APP_LPTIMER_INTERRUPT_PRIORITY      (1U)

#if ( configGENERATE_RUN_TIME_STATS == 1 )
#define TCPWM_TIMER_INT_PRIORITY            (1U)
#endif

/*******************************************************************************
* Global Variables
*******************************************************************************/
/* IPC communication: ISR enqueues, cm55_gfx_task drains via handle_system_event().
 * The FreeRTOS queue holds each message until the task reads it, so a burst of
 * messages arriving faster than the task runs is preserved in order rather than
 * overwriting one another. Overflow beyond the queue depth is dropped and
 * counted in cm55_ipc_rx_drop_count. */
QueueHandle_t cm55_ipc_rx_queue = NULL;

/* Counts CM33->CM55 messages dropped because the rx queue was full when the
 * ISR tried to enqueue them. A confirmed Cy_IPC_Pipe_SendMessage() only proves
 * the pipe accepted the message, not that xQueueSendFromISR() succeeded.
 * app_gfx_disp.c logs this from task context so a non-zero count is visible. */
volatile uint32_t cm55_ipc_rx_drop_count = 0;

/*******************************************************************************
* FreeRTOS Task Handles
*******************************************************************************/
TaskHandle_t rtos_cm55_gfx_task_handle = NULL;
TaskHandle_t rtos_cm55_voice_task_handle = NULL;
TaskHandle_t rtos_cm55_sensor_task_handle = NULL;

/*******************************************************************************
* I2C and Hardware Communication
*******************************************************************************/
cy_stc_scb_i2c_context_t disp_touch_i2c_controller_context;

cy_stc_sysint_t disp_touch_i2c_controller_irq_cfg =
{
    .intrSrc      = CYBSP_I2C_DISPLAY_CONTROLLER_IRQ,
    .intrPriority = I2C_CONTROLLER_IRQ_PRIORITY,
};

/* Mutex to guard the I2C instance for Sensor/Touchpad */
SemaphoreHandle_t i2c_mutex = NULL;

/* LPTimer HAL object */
static mtb_hal_lptimer_t lptimer_obj;

/* RTC HAL object */
static mtb_hal_rtc_t rtc_obj;

mtb_hal_i2c_t CYBSP_I2C_CONTROLLER_hal_obj;
cy_stc_scb_i2c_context_t CYBSP_I2C_CONTROLLER_context;

char fw_current_version[MAX_FW_VERSION_LEN];

/*******************************************************************************
* Function Prototypes
*******************************************************************************/
uint32_t get_time_ms(void);

/*******************************************************************************
 *                              FUNCTION DEFINITIONS
 ******************************************************************************/
#if ( configGENERATE_RUN_TIME_STATS == 1 )
/*******************************************************************************
* Function Name: setup_run_time_stats_timer
********************************************************************************
* Summary:
*  This function configuresTCPWM 0 GRP 0 Counter 0 as the timer source for  
*  FreeRTOS runtime statistics.
*
* Parameters:
*  void
*
* Return:
*  void
*
*******************************************************************************/
void setup_run_time_stats_timer(void)
{
    /* Initialze TCPWM block with required timer configuration */
    if (CY_TCPWM_SUCCESS != Cy_TCPWM_Counter_Init(CYBSP_GENERAL_PURPOSE_TIMER_HW, 
        CYBSP_GENERAL_PURPOSE_TIMER_NUM, 
        &CYBSP_GENERAL_PURPOSE_TIMER_config))
    {
        handle_app_error();
    }

    /* Enable the initialized counter */
    Cy_TCPWM_Counter_Enable(CYBSP_GENERAL_PURPOSE_TIMER_HW, 
                            CYBSP_GENERAL_PURPOSE_TIMER_NUM);

    /* Start the counter */
    Cy_TCPWM_TriggerStart_Single(CYBSP_GENERAL_PURPOSE_TIMER_HW, 
                                 CYBSP_GENERAL_PURPOSE_TIMER_NUM);
}


/*******************************************************************************
* Function Name: get_run_time_counter_value
********************************************************************************
* Summary:
*  Function to fetch run time counter value. This will be used by FreeRTOS for 
*  run time statistics calculation.
*
* Parameters:
*  void
*
* Return:
*  uint32_t: TCPWM 0 GRP 0 Counter 0 value
*
*******************************************************************************/
CY_SECTION(".cy_itcm") uint32_t get_run_time_counter_value(void)
{
   return (Cy_TCPWM_Counter_GetCounter(CYBSP_GENERAL_PURPOSE_TIMER_HW, 
                                       CYBSP_GENERAL_PURPOSE_TIMER_NUM));
}


/*******************************************************************************
* Function Name: calculate_idle_percentage
********************************************************************************
* Summary:
*  Function to calculate CPU idle percentage. This function is used by LVGL to
*  showcase CPU usage.
*
* Parameters:
*  void
*
* Return:
*  uint32_t: CPU idle percentage
*
*******************************************************************************/
CY_SECTION(".cy_itcm") uint32_t calculate_idle_percentage(void)
{
    static uint32_t previousIdleTime = 0;
    static TickType_t previousTick = 0;
    uint32_t time_diff = 0;
    uint32_t idle_percent = 0;

    uint32_t currentIdleTime = ulTaskGetIdleRunTimeCounter();
    TickType_t currentTick = portGET_RUN_TIME_COUNTER_VALUE();

    time_diff = currentTick - previousTick;

    if ((currentIdleTime >= previousIdleTime) && (currentTick > previousTick))
    {
        idle_percent = ((currentIdleTime - previousIdleTime) * 100) / time_diff;
    }
    else if ((currentIdleTime >= previousIdleTime) && (currentTick < previousTick))
    {
        time_diff = 10000 - previousTick + currentTick;
        idle_percent = ((currentIdleTime - previousIdleTime) * 100) / time_diff;
    }
    previousIdleTime = ulTaskGetIdleRunTimeCounter();
    previousTick = portGET_RUN_TIME_COUNTER_VALUE();

    return idle_percent;
}
#endif

/*******************************************************************************
* Function Name: cm33_msg_callback
********************************************************************************
* Summary:
*  Callback function called when endpoint-2 (CM55) has received a message
*
* Parameters:
*  msg_data: Message data received throuig IPC
*
* Return :
*  void
*
*******************************************************************************/
CY_SECTION(".cy_itcm") void cm55_msg_callback(uint32_t * msgData)
{
    BaseType_t higher_prio_task_woken = pdFALSE;

    if (msgData != NULL)
    {
        if (xQueueSendFromISR(cm55_ipc_rx_queue, msgData, &higher_prio_task_woken) != pdPASS)
        {
            /* Queue full: drop the message rather than overwrite an
             * unread one (the snapshot-buffer approach this replaces
             * could silently do exactly that under a fast message burst). */
            cm55_ipc_rx_drop_count++;
        }
    }

    portYIELD_FROM_ISR(higher_prio_task_woken);
}

/*******************************************************************************
* Function Name: lptimer_interrupt_handler
********************************************************************************
* Summary:
*  Interrupt handler function for LPTimer instance.
*
* Parameters:
*  void
*
* Return:
*  void
*
*******************************************************************************/
CY_SECTION(".cy_itcm") static void lptimer_interrupt_handler(void)
{
    mtb_hal_lptimer_process_interrupt(&lptimer_obj);
}


/*******************************************************************************
* Function Name: setup_tickless_idle_timer
********************************************************************************
* Summary:
*  1. This function first configures and initializes an interrupt for LPTimer.
*  2. Then it initializes the LPTimer HAL object to be used in the RTOS
*     tickless idle mode implementation to allow the device enter deep sleep
*     when idle task runs. LPTIMER_1 instance is configured for CM55 CPU.
*  3. It then passes the LPTimer object to abstraction RTOS library that
*     implements tickless idle mode
*
* Parameters:
*  void
*
* Return:
*  void
*
*******************************************************************************/
static void setup_tickless_idle_timer(void)
{
    /* Interrupt configuration structure for LPTimer */
    cy_stc_sysint_t lptimer_intr_cfg =
    {
        .intrSrc = CYBSP_CM55_LPTIMER_1_IRQ,
        .intrPriority = APP_LPTIMER_INTERRUPT_PRIORITY
    };

    /* Initialize the LPTimer interrupt and specify the interrupt handler. */
    cy_en_sysint_status_t interrupt_init_status =
                                    Cy_SysInt_Init(&lptimer_intr_cfg,
                                                    lptimer_interrupt_handler);

    /* LPTimer interrupt initialization failed. Stop program execution. */
    if (CY_SYSINT_SUCCESS != interrupt_init_status)
    {
        APP_ERROR(1);
    }

    /* Enable NVIC interrupt. */
    NVIC_EnableIRQ(lptimer_intr_cfg.intrSrc);

    /* Initialize the MCWDT block */
    cy_en_mcwdt_status_t mcwdt_init_status =
                                    Cy_MCWDT_Init(CYBSP_CM55_LPTIMER_1_HW,
                                                &CYBSP_CM55_LPTIMER_1_config);

    /* MCWDT initialization failed. Stop program execution. */
    if (CY_MCWDT_SUCCESS != mcwdt_init_status)
    {
        APP_ERROR(mcwdt_init_status);
    }

    /* Enable MCWDT instance */
    Cy_MCWDT_Enable(CYBSP_CM55_LPTIMER_1_HW,
                    CY_MCWDT_CTR_Msk,
                    LPTIMER_1_WAIT_TIME_USEC);

    /* Setup LPTimer using the HAL object and desired configuration as defined
     * in the device configurator. */
    cy_rslt_t result = mtb_hal_lptimer_setup(&lptimer_obj,
                                            &CYBSP_CM55_LPTIMER_1_hal_config);

    /* LPTimer setup failed. Stop program execution. */
    if (CY_RSLT_SUCCESS != result)
    {
        APP_ERROR(result);
    }

    /* Pass the LPTimer object to abstraction RTOS library that implements
     * tickless idle mode */
    cyabs_rtos_set_lptimer(&lptimer_obj);
}

/*******************************************************************************
* Function Name: disp_touch_i2c_controller_interrupt
********************************************************************************
* Summary:
*  I2C controller ISR which invokes Cy_SCB_I2C_Interrupt to perform I2C transfer
*  as controller.
*
* Parameters:
*  void
*
* Return:
*  void
*
*******************************************************************************/
CY_SECTION(".cy_itcm") static void disp_touch_i2c_controller_interrupt(void)
{
    Cy_SCB_I2C_Interrupt(CYBSP_I2C_DISPLAY_CONTROLLER_HW, &disp_touch_i2c_controller_context);
}

static void init_i2c_controller(void)
{
    cy_en_scb_i2c_status_t i2c_result = CY_SCB_I2C_SUCCESS;
    cy_en_sysint_status_t sysint_status = CY_SYSINT_SUCCESS;

    i2c_result = Cy_SCB_I2C_Init(CYBSP_I2C_DISPLAY_CONTROLLER_HW,
                                &CYBSP_I2C_DISPLAY_CONTROLLER_config,
                                &disp_touch_i2c_controller_context);

    if (CY_SCB_I2C_SUCCESS != i2c_result)
    {
        LOG_ERROR(CYLF_DEF, "I2C controller initialization failed\r\n");
        APP_ERROR(1);
    }

    /* Initialize the I2C interrupt */
    sysint_status = Cy_SysInt_Init(&disp_touch_i2c_controller_irq_cfg,
                                   &disp_touch_i2c_controller_interrupt);

    if (CY_SYSINT_SUCCESS != sysint_status)
    {
        LOG_ERROR(CYLF_DEF, "I2C controller interrupt initialization failed\r\n");
        APP_ERROR(1);
    }

    /* Enable the I2C interrupts */
    NVIC_EnableIRQ((IRQn_Type)disp_touch_i2c_controller_irq_cfg.intrSrc);

    /* Enable the I2C */
    Cy_SCB_I2C_Enable(CYBSP_I2C_DISPLAY_CONTROLLER_HW);

    i2c_result = mtb_hal_i2c_setup(&CYBSP_I2C_DISPLAY_CONTROLLER_hal_obj,
                                   &CYBSP_I2C_DISPLAY_CONTROLLER_hal_config,
                                   &disp_touch_i2c_controller_context,
                                   NULL);

    if (CY_SCB_I2C_SUCCESS != i2c_result)
    {
        LOG_ERROR(CYLF_DEF, "I2C HAL setup failed with error code: 0x%08X\r\n", (unsigned int)i2c_result);
        APP_ERROR(i2c_result);
    }
}

void cy_rtc_controller()
{
    cy_rslt_t result = CY_RSLT_SUCCESS;
    cy_en_scb_i2c_status_t i2c_result = CY_SCB_I2C_SUCCESS;

    i2c_result = Cy_SCB_I2C_Init(CYBSP_I2C_CONTROLLER_HW,
            &CYBSP_I2C_CONTROLLER_config,
            &CYBSP_I2C_CONTROLLER_context);

    if(CY_SCB_I2C_SUCCESS != i2c_result)
    {
        printf(" Error : I2C initialization failed !!\r\n");
        handle_app_error();
    }

    /* Configure the I2C interface with the desired clock frequency */
    result = mtb_hal_i2c_setup(&CYBSP_I2C_CONTROLLER_hal_obj,
            &CYBSP_I2C_CONTROLLER_hal_config,
            &CYBSP_I2C_CONTROLLER_context,
            NULL);

    if(CY_RSLT_SUCCESS != result)
    {
        printf(" Error : I2C setup failed !!\r\n");
        handle_app_error();
    }

    rtc_boot_once();
}

/*******************************************************************************
* Function Name: get_time_ms
********************************************************************************
* Summary:
*  This function gets the current time in milliseconds in FreeRTOS environment.
*
* Parameters:
*  void
*
* Return:
*  uint32_t: current time in milliseconds
*
*******************************************************************************/
uint32_t get_time_ms(void)
{
    /* Convert tick count to milliseconds */
    return (uint32_t) (xTaskGetTickCount() * portTICK_PERIOD_MS);
}
/*******************************************************************************
* Function Name: setup_clib_support
********************************************************************************
* Summary:
*    1. This function configures and initializes the Real-Time Clock (RTC)).
*    2. It then initializes the RTC HAL object to enable CLIB support library 
*       to work with the provided Real-Time Clock (RTC) module.
*
* Parameters:
*  void
*
* Return:
*  void
*
*******************************************************************************/
static void setup_clib_support(void)
{
    /* RTC Initialization is done in CM33 non-secure project */

    /* Initialize the ModusToolbox CLIB support library */
    mtb_clib_support_init(&rtc_obj);
}

/*******************************************************************************
* Function Name: main
********************************************************************************
* Summary:
*  This is the main function for CM55 non-secure application.
*    1. It initializes the device and board peripherals.
*    2. It sets up the LPtimer instance for CM55 CPU and initializes debug UART. 
*    3. It creates the FreeRTOS application task 'cm55_gfx_task'.
*    4. It starts the RTOS task scheduler.
*
* Parameters:
*  void
*
* Return:
*  int
*
*******************************************************************************/
int main(void)
{
    cy_rslt_t result       = CY_RSLT_SUCCESS;
    BaseType_t task_return = pdFAIL;
    cy_en_ipc_pipe_status_t pipeStatus;

    /* Initialize the device and board peripherals */
    result = cybsp_init();

    /* Board init failed. Stop program execution */
    if (CY_RSLT_SUCCESS != result)
    {
        APP_ERROR(result);
    }

    /* Setup CLIB support library. */
    setup_clib_support();
    /* Setup the LPTimer instance for CM55 */
    setup_tickless_idle_timer();

    /* Initialize retarget-io middleware */
    init_retarget_io();
    /* Enable global interrupts */
    __enable_irq();

    result = cy_log_init(CY_LOG_ERR, NULL, NULL);
    if (CY_RSLT_SUCCESS != result)
    {
        printf("cy_log_init failed with Error : [0x%X] \n", (unsigned int) result);
    }
    else
    {
        cy_log_set_facility_level(CYLF_DRIVER, CY_LOG_WARNING);
        cy_log_set_facility_level(CYLF_DEF, CY_LOG_INFO);
        cy_log_set_facility_level(CYLF_MIDDLEWARE, CY_LOG_WARNING);
    }

    /* Initialize RTC */
    app_rtc_init();

    /* Update FW version variable */
    sprintf(fw_current_version, "%d.%d.%d", APP_VERSION_MAJOR, APP_VERSION_MINOR, APP_VERSION_BUILD);

    /* Setup IPC communication for CM55 */
    cm55_ipc_communication_setup();

    Cy_SysLib_Delay(50);

    /* Must exist before the callback below can ever fire. Item size matches
     * ipc_msg_cm33_to_cm55_t (the CM33->CM55 struct), not the larger
     * ipc_msg_t used for the opposite direction. */
    cm55_ipc_rx_queue = xQueueCreate(CM55_IPC_RX_QUEUE_LENGTH, sizeof(ipc_msg_cm33_to_cm55_t));
    if (NULL == cm55_ipc_rx_queue)
    {
        APP_ERROR(1);
    }

    /* Register a callback function to handle events on the CM55 IPC pipe */
    pipeStatus = Cy_IPC_Pipe_RegisterCallback(CM55_IPC_PIPE_EP_ADDR, &cm55_msg_callback,
                                             (uint32_t)CM55_IPC_PIPE_CLIENT_ID);

    if (CY_IPC_PIPE_SUCCESS != pipeStatus)
    {
        APP_ERROR(pipeStatus);
    }

    /* Initialize I2C SCB */
    init_i2c_controller();
    
    /* Create a binary semaphore to act as an I2C mutex
     * guarding touchpad and sensor */
    i2c_mutex = xSemaphoreCreateMutex();
    if (i2c_mutex == NULL)
    {
        LOG_ERROR(CYLF_DEF, "I2C mutex creation failed\r\n");
        APP_ERROR(1);
    }
    xSemaphoreGive(i2c_mutex);

    /* Initialize Speaker */
    app_speaker_init();

    /* Initialize Emulated EEPROM */
    app_eeprom_init();

    /* Read configuration from Emulated EEPROM */
    device_settings_t rd_settings = {0};
    app_eeprom_read(&rd_settings);

    if (rd_settings.is_available != true)
    {
        device_settings_t settings = {0};

        /* Load default configuration */
        get_default_device_setting(&settings);
        settings.is_available = true;

        app_eeprom_write(&settings);
        set_current_device_setting(&rd_settings);
    }
    else
    {
        set_current_device_setting(&rd_settings);
    }

    dev_info.environment.target_temp = dev_info.environment.current_temp;

    /* Create the FreeRTOS Task */
    /* Start sensor task */
    app_sensor_task_init();

    LOG_INFO(CYLF_DEF, "[main] Creating CM55 GFX task\r\n");
    /* Start GFX task */
    task_return = xTaskCreate(cm55_gfx_task, GFX_TASK_NAME,
                             GFX_TASK_STACK_SIZE, NULL,
                             GFX_TASK_PRIORITY, &rtos_cm55_gfx_task_handle);

    if (pdPASS != task_return)
    {
        LOG_ERROR(CYLF_DEF, "[main] Error: Failed to create cm55_gfx_task. Return code: %d\r\n", task_return);
        APP_ERROR(1);
    }
    LOG_INFO(CYLF_DEF, "[main] GFX task created successfully\r\n");

    /* Start Voice Assistant task */
#if defined (USE_VOICE_ASSISTANT)
    LOG_INFO(CYLF_DEF, "[main] Creating voice assistant task\r\n");
    task_return = xTaskCreate(voice_assistant_task,
                             VOICE_ASSISTANT_TASK_NAME,
                             VOICE_ASSISTANT_TASK_STACK_SIZE, NULL,
                             VOICE_ASSISTANT_TASK_PRIORITY, &rtos_cm55_voice_task_handle);

    if (pdPASS != task_return)
    {
        LOG_ERROR(CYLF_DEF, "[main] Error: Failed to create voice_assistant_task. Return code: %d\r\n", task_return);
        APP_ERROR(1);
    }
    LOG_INFO(CYLF_DEF, "[main] Voice assistant task created successfully\r\n");
#endif /* USE_VOICE_ASSISTANT */

    LOG_INFO(CYLF_DEF, "Thermostat Application Started Version: %s\r\n", fw_current_version);

    /* Start the RTOS Scheduler */
    vTaskStartScheduler();

    /* Should never get here! */
    handle_app_error();
}

/* [] END OF FILE */
