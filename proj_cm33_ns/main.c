/*******************************************************************************
* File Name:   main.c
*
* Description: This is the source code for MQTT Client example running on CM33 CPU.
*
* Related Document: See README.md
*
*******************************************************************************
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

/* Header file includes */
#include "wireless_manager.h"
#include "app_ui_receiver.h"
#include "app_radar.h"
#include "secure_http_client.h"

/******************************************************************************
 * Macros
 ******************************************************************************/
#define HTTPS_TASK_NAME                             ("HTTPS Client")
#define HTTPS_CLIENT_TASK_STACK_SIZE                RTOS_STACK_SIZE_IN_BYTE((1024*5U))
#define HTTPS_CLIENT_TASK_PRIORITY                  (4U)

/* The timeout value in microsecond used to wait for core to be booted */
#define CM55_BOOT_WAIT_TIME_US            (10U)
/* App boot address for CM55 project */
#define CM55_APP_BOOT_ADDR          (CYMEM_CM33_0_m55_nvm_START + \
                                        CYBSP_MCUBOOT_HEADER_SIZE)
/* Enabling or disabling a MCWDT requires a wait time of upto 2 CLK_LF cycles
 * to come into effect. This wait time value will depend on the actual CLK_LF
 * frequency set by the BSP.
 */
#define LPTIMER_0_WAIT_TIME_USEC           (62U)
/* Define the LPTimer interrupt priority number. '1' implies highest priority.
 */
#define APP_LPTIMER_INTERRUPT_PRIORITY      (1U)

/*******************************************************************************
 * Global Variables
 ******************************************************************************/
/* LPTimer HAL object */
static mtb_hal_lptimer_t lptimer_obj;

/* RTC HAL object */
static mtb_hal_rtc_t rtc_obj;

char current_fw_version[MAX_FW_VERSION_LEN] = "-.-.-";
ipc_msg_t *ipc_recv_msg;

/*****************************************************************************
 * Function Definitions
 *****************************************************************************/

/*******************************************************************************
* Function Name: cm33_msg_callback
********************************************************************************
* Summary:
*  Callback function called when endpoint-1 (CM33) has received a message
*
* Parameters:
*  msg_data: Message data received throuig IPC
*
* Return :
*  void
*
*******************************************************************************/
void cm33_msg_callback(uint32_t * msg_data)
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    BaseType_t xStatus;

    if (msg_data != NULL)
    {
        /* Cast the message received to the IPC structure */
        ipc_recv_msg = (ipc_msg_t *) msg_data;

        /* Queue the IPC message to be processed later */
        xStatus = xQueueSendFromISR(xUiRxQueue, ipc_recv_msg, &xHigherPriorityTaskWoken);
        if (xStatus != pdPASS)
        {
            LOG_ERROR(CYLF_DEF, "CM33 IPC Callback -> UI RX Queue Full\n");
        }

        /* Perform context switch if High priority task unblocked */
        portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
    }
}

/*******************************************************************************
* Function Name: lptimer_interrupt_handler
********************************************************************************
* Summary:
* Interrupt handler function for LPTimer instance.
*
* Parameters:
*  void
*
* Return:
*  void
*
*******************************************************************************/
static void lptimer_interrupt_handler(void)
{
    mtb_hal_lptimer_process_interrupt(&lptimer_obj);
}

/*******************************************************************************
* Function Name: setup_tickless_idle_timer
********************************************************************************
* Summary:
* 1. This function first configures and initializes an interrupt for LPTimer.
* 2. Then it initializes the LPTimer HAL object to be used in the RTOS
*    tickless idle mode implementation to allow the device enter deep sleep
*    when idle task runs. LPTIMER_0 instance is configured for CM33 CPU.
* 3. It then passes the LPTimer object to abstraction RTOS library that
*    implements tickless idle mode
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
        .intrSrc = CYBSP_CM33_LPTIMER_0_IRQ,
        .intrPriority = APP_LPTIMER_INTERRUPT_PRIORITY
    };

    /* Initialize the LPTimer interrupt and specify the interrupt handler. */
    cy_en_sysint_status_t interrupt_init_status =
                                    Cy_SysInt_Init(&lptimer_intr_cfg,
                                                    lptimer_interrupt_handler);

    /* LPTimer interrupt initialization failed. Stop program execution. */
    if(CY_SYSINT_SUCCESS != interrupt_init_status)
    {
/* handle_ERROR_CHECK(); */
        ERROR_CHECK(interrupt_init_status);
    }

    /* Enable NVIC interrupt. */
    NVIC_EnableIRQ(lptimer_intr_cfg.intrSrc);

    /* Initialize the MCWDT block */
    cy_en_mcwdt_status_t mcwdt_init_status =
                                    Cy_MCWDT_Init(CYBSP_CM33_LPTIMER_0_HW,
                                                &CYBSP_CM33_LPTIMER_0_config);

    /* MCWDT initialization failed. Stop program execution. */
    if(CY_MCWDT_SUCCESS != mcwdt_init_status)
    {
/* handle_ERROR_CHECK(); */
        ERROR_CHECK(mcwdt_init_status);
    }

    /* Enable MCWDT instance */
    Cy_MCWDT_Enable(CYBSP_CM33_LPTIMER_0_HW,
                    CY_MCWDT_CTR_Msk,
                    LPTIMER_0_WAIT_TIME_USEC);

    /* Setup LPTimer using the HAL object and desired configuration as defined
     * in the device configurator. */
    cy_rslt_t result = mtb_hal_lptimer_setup(&lptimer_obj,
                                            &CYBSP_CM33_LPTIMER_0_hal_config);

    /* LPTimer setup failed. Stop program execution. */
    if(CY_RSLT_SUCCESS != result)
    {
/* handle_ERROR_CHECK(); */
        ERROR_CHECK(result);
    }

    /* Pass the LPTimer object to abstraction RTOS library that implements
     * tickless idle mode
     */
    cyabs_rtos_set_lptimer(&lptimer_obj);
}

/*******************************************************************************
* Function Name: setup_clib_support
********************************************************************************
* Summary:
*    1. This function configures and initializes the Real-Time Clock (RTC).
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
    /* RTC Initialization */
    Cy_RTC_Init(&CYBSP_RTC_config);
    Cy_RTC_SetDateAndTime(&CYBSP_RTC_config);

    /* Initialize the ModusToolbox CLIB support library */
    mtb_clib_support_init(&rtc_obj);
}

/******************************************************************************
 * Function Name: main
 ******************************************************************************
 * Summary:
 *  System entrance point. This function initializes retarget IO, RTC, sets up 
 *  the MQTT client task, enables CM55 and then starts the RTOS scheduler.
 *
 * Parameters:
 *  void
 *
 * Return:
 *  int
 *
 ******************************************************************************/

int main(void)
{
    cy_rslt_t result;
    cy_en_ipc_pipe_status_t pipeStatus;
 

    /* Initialize the board support package. */
    result = cybsp_init();
    CY_ASSERT(CY_RSLT_SUCCESS == result);

    /* To avoid compiler warnings. */
    CY_UNUSED_PARAMETER(result);

    /* Enable global interrupts. */
    __enable_irq();

    /* Setup IPC communication for CM33 */
    cm33_ipc_communication_setup();

    Cy_SysLib_Delay(50);

    /* Register a callback function to handle events on the CM33 IPC pipe */
    pipeStatus = Cy_IPC_Pipe_RegisterCallback(CM33_IPC_PIPE_EP_ADDR, &cm33_msg_callback,
                                              (uint32_t)CM33_IPC_PIPE_CLIENT_ID);

    if(CY_IPC_PIPE_SUCCESS != pipeStatus)
    {
/* handle_ERROR_CHECK(); */
        ERROR_CHECK(pipeStatus);
    }

    /* Setup the LPTimer instance for CM33 CPU. */
    setup_tickless_idle_timer();

    /* Initialize retarget-io middleware */
    init_retarget_io();
    /* Setup CLIB support library. */
    setup_clib_support();
    /* Default for all logging to WARNING */
    result = cy_log_init(CY_LOG_WARNING, NULL, NULL);
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

    /* \x1b[2J\x1b[;H - ANSI ESC sequence to clear screen. */
    LOG_INFO(CYLF_DEF, "\x1b[2J\x1b[;H");
    LOG_INFO(CYLF_DEF, "===============================================================\n");
    LOG_INFO(CYLF_DEF, "Thermostat Application Started Version: <V%d.%d.%d>\n", APP_VERSION_MAJOR, APP_VERSION_MINOR, APP_VERSION_BUILD);
    LOG_INFO(CYLF_DEF, "===============================================================\n\n");

    sprintf(current_fw_version, "%d.%d.%d", APP_VERSION_MAJOR, APP_VERSION_MINOR, APP_VERSION_BUILD);
    
    /* Enable CM55. CY_CORTEX_M55_APPL_ADDR must be updated if CM55 memory layout is changed. */
    Cy_SysEnableCM55(MXCM55, CM55_APP_BOOT_ADDR, CM55_BOOT_WAIT_TIME_US);


    ui_rx_thread_init();

    start_radar_accquisition_task();

    /* Initialize WiFi Tasks */
    result = xTaskCreate(wifi_task, "WiFi Task", WIFI_TASK_STACK_SIZE, NULL,
                                                 WIFI_TASK_PRIORITY, &wifi_task_handle); 
    if(pdPASS != result)
    {
        ERROR_CHECK(result);
    }

    /* Create the MQTT Client task. */
    result = xTaskCreate(mqtt_client_task, MQTT_TASK_NAME, MQTT_CLIENT_TASK_STACK_SIZE, NULL,
                                                            MQTT_CLIENT_TASK_PRIORITY, NULL);
    if(pdPASS != result)
    {
        ERROR_CHECK(result);
    }


    result = xTaskCreate(https_client_task, HTTPS_TASK_NAME, HTTPS_CLIENT_TASK_STACK_SIZE, NULL,
                                                             HTTPS_CLIENT_TASK_PRIORITY, NULL);

    if( pdPASS == result )
    {
        /* Start the FreeRTOS scheduler. */
        vTaskStartScheduler();
        
        /* Should never get here. */
/* handle_ERROR_CHECK(); */
        ERROR_CHECK(result);
    }
    else
    {
/* handle_ERROR_CHECK(); */
        ERROR_CHECK(result);
    }
}

/* [] END OF FILE */
