/*******************************************************************************
 * File Name:  app_radar.c
 *
 * Description: Radar presence detection module implementation.
 *
 * This file implements the radar sensor initialization, data acquisition,
 * and presence detection processing.
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

/*******************************************************************************
 *                                INCLUDES
 ******************************************************************************/
#include "app_radar.h"
#include "xensiv_radar_presence.h"
#include "xensiv_bgt60trxx_mtb.h"
#define XENSIV_BGT60TRXX_CONF_IMPL
#include "presence_radar_settings.h"
#include "app_common.h"
#include "comm_manager.h"

/*******************************************************************************
 *                              CONSTANTS
 ******************************************************************************/
#define PROCESSING_TASK_NAME                "radar_processing_task"
#define PROCESSING_TASK_STACK_SIZE          RTOS_STACK_SIZE_IN_BYTE(1024*3U)
#define PROCESSING_TASK_PRIORITY            2

#define ACCQUISITION_TASK_NAME              "radar_acquisition_task"
#define ACCQUISITION_TASK_STACK_SIZE        RTOS_STACK_SIZE_IN_BYTE(1024*3U)
#define ACCQUISITION_TASK_PRIORITY          2

#define INIT_SUCCESS            (0UL)
#define INIT_FAILURE            (1UL)
#define XENSIV_BGT60TRXX_IRQ_PRIORITY                      (3U)

#define NUM_SAMPLES_PER_FRAME               (XENSIV_BGT60TRXX_CONF_NUM_RX_ANTENNAS *\
                                             XENSIV_BGT60TRXX_CONF_NUM_CHIRPS_PER_FRAME *\
                                             XENSIV_BGT60TRXX_CONF_NUM_SAMPLES_PER_CHIRP)

#define NUM_CHIRPS_PER_FRAME                XENSIV_BGT60TRXX_CONF_NUM_CHIRPS_PER_FRAME
#define NUM_SAMPLES_PER_CHIRP               XENSIV_BGT60TRXX_CONF_NUM_SAMPLES_PER_CHIRP

#define SPI_INTR_NUM        ((IRQn_Type) CYBSP_RSPI_IRQ)
#define SPI_INTR_PRIORITY   (2U)

#define ENABLE_RADAR_TIMING_LOG 0U
#define pdTICKS_TO_MS(xTicks) ((xTicks) * 1000 / configTICK_RATE_HZ)

/*******************************************************************************
 *                              GLOBAL VARIABLE
 ******************************************************************************/
static cy_en_scb_spi_status_t init_status;
static cy_stc_scb_spi_context_t SPI_context;
static xensiv_bgt60trxx_mtb_t sensor;
static cy_stc_sysint_t irq_cfg;

static uint16_t bgt60_buffer[NUM_SAMPLES_PER_FRAME] __attribute__((aligned(2)));
static float32_t frame[NUM_SAMPLES_PER_FRAME];
static float32_t avg_chirp[NUM_SAMPLES_PER_CHIRP];

/* Task handlers */
static TaskHandle_t radar_processing_tsk_hdlr;
static TaskHandle_t radar_accqusition_tsk_hdlr;

/****************************************************************************
 *                              FUNCTION DECLARATIONS
 ***************************************************************************/

/*******************************************************************************
 * Function Name: init_sensor
 ********************************************************************************
 * Summary:
 * This function configures the SPI interface, initializes radar and interrupt
 * service routine to indicate the availability of radar data.
 *
 * Parameters:
 *  void
 *
 * Return:
 *  Success or error
 *
 *******************************************************************************/
static int32_t init_sensor(void);

static void mSPI_Interrupt(void);

static void processing_task(void *pvParameters);
static void accquisition_task(void *pvParameters);


/*******************************************************************************
* Function Name: presence_detection_cb
********************************************************************************
* Summary:
* This is the callback function o indicate presence/absence events on terminal
* and LEDs.
* Parameters:
*  void
*
* Return:
*  None
*
*******************************************************************************/
static void presence_detection_cb(xensiv_radar_presence_handle_t handle,
                           const xensiv_radar_presence_event_t* event,
                           void *data);

/*******************************************************************************
* Function Name: xensiv_bgt60trxx_interrupt_handler
********************************************************************************
* Summary:
* This is the interrupt handler to react on sensor indicating the availability
* of new data
*    1. Notifies main task on interrupt from sensor
*
* Parameters:
*  void
*
* Return:
*  none
*
*******************************************************************************/
static void xensiv_bgt60trxx_mtb_interrupt_handler(void);

/*******************************************************************************
 *                              FUNCTION DEFINITIONS
 ******************************************************************************/


/*******************************************************************************
* Function Name: init_sensor
********************************************************************************
* Summary:
*  Initializes the radar sensor hardware and SPI interface.
*
* Parameters:
*  void
*
* Return:
*  int32_t - INIT_SUCCESS on success, INIT_FAILURE or error code on failure.
*
*******************************************************************************/
static int32_t init_sensor(void)
{
    cy_rslt_t result = CY_RSLT_SUCCESS;
    uint32_t status = INIT_SUCCESS;

    sensor.iface.scb_inst = CYBSP_RSPI_HW;
    sensor.iface.spi = &SPI_context;
    sensor.iface.sel_port = CYBSP_RSPI_CS_PORT;
    sensor.iface.sel_pin = CYBSP_RSPI_CS_PIN;
    sensor.iface.rst_port = CYBSP_RADAR_RESET_PORT;
    sensor.iface.rst_pin = CYBSP_RADAR_RESET_PIN;
    sensor.iface.irq_port = CYBSP_RADAR_INT_PORT;
    sensor.iface.irq_pin = CYBSP_RADAR_INT_PIN;
    sensor.iface.irq_num = CYBSP_RADAR_INT_IRQ;

    irq_cfg.intrSrc = sensor.iface.irq_num;
    irq_cfg.intrPriority = XENSIV_BGT60TRXX_IRQ_PRIORITY;

    /* Initialize the SPI interface to BGT60. */
    init_status = Cy_SCB_SPI_Init(CYBSP_RSPI_HW, &CYBSP_RSPI_config, &SPI_context);

    /* If the initialization fails, update status */
    if ( CY_SCB_SPI_SUCCESS != init_status )
    {
        return INIT_FAILURE;
    }

    if (status == INIT_SUCCESS)
    {
        cy_stc_sysint_t spiIntrConfig =
        {
            .intrSrc      = SPI_INTR_NUM,
            .intrPriority = SPI_INTR_PRIORITY,
        };

        Cy_SysInt_Init(&spiIntrConfig, &mSPI_Interrupt);
        NVIC_EnableIRQ(SPI_INTR_NUM);

        /* Set active target select to line 0 */
        Cy_SCB_SPI_SetActiveSlaveSelect(CYBSP_SPI_CONTROLLER_2_HW, CY_SCB_SPI_SLAVE_SELECT0);
        /* Enable SPI Controller block. */
        Cy_SCB_SPI_Enable(CYBSP_RSPI_HW);
    }

    /* Reduce drive strength to improve EMI */
    Cy_GPIO_SetSlewRate(CYBSP_RSPI_MOSI_PORT, CYBSP_RSPI_MOSI_PIN, CY_GPIO_SLEW_FAST);
    Cy_GPIO_SetDriveSel(CYBSP_RSPI_MOSI_PORT, CYBSP_RSPI_MOSI_PIN, CY_GPIO_DRIVE_1_8);
    Cy_GPIO_SetSlewRate(CYBSP_RSPI_CLK_PORT, CYBSP_RSPI_CLK_PIN, CY_GPIO_SLEW_FAST);
    Cy_GPIO_SetDriveSel(CYBSP_RSPI_CLK_PORT, CYBSP_RSPI_CLK_PIN, CY_GPIO_DRIVE_1_8);

    result = xensiv_bgt60trxx_mtb_init(&sensor, register_list, XENSIV_BGT60TRXX_CONF_NUM_REGS);
    if(result != CY_RSLT_SUCCESS)
    {
        printf("ERROR: xensiv_bgt60trxx_mtb_init failed\n");
        return result;
    }

    /* The sensor will generate an interrupt once the sensor FIFO level is
       NUM_SAMPLES_PER_FRAME */
    result = xensiv_bgt60trxx_mtb_interrupt_init(&sensor, NUM_SAMPLES_PER_FRAME);
    if(result != CY_RSLT_SUCCESS)
    {
        LOG_ERROR(CYLF_DEF, "xensiv_bgt60trxx_mtb_interrupt_init failed\n");
        return result;
    }

    Cy_SysInt_Init(&irq_cfg, xensiv_bgt60trxx_mtb_interrupt_handler);

    NVIC_ClearPendingIRQ(irq_cfg.intrSrc);
    NVIC_EnableIRQ(irq_cfg.intrSrc);

    if (xensiv_bgt60trxx_start_frame(&sensor.dev, true) != XENSIV_BGT60TRXX_STATUS_OK)
    {
        return INIT_FAILURE;
    }

    return INIT_SUCCESS;
}

/*******************************************************************************
* Function Name: mSPI_Interrupt
********************************************************************************
* Summary:
*  SPI interrupt handler for radar sensor communication.
*
* Parameters:
*  void
*
* Return:
*  void
*
*******************************************************************************/
static void mSPI_Interrupt(void)
{
    Cy_SCB_SPI_Interrupt(CYBSP_RSPI_HW, &SPI_context);
}

/*******************************************************************************
* Function Name: start_radar_processing_task
********************************************************************************
* Summary:
*  Creates the radar data processing FreeRTOS task.
*
* Parameters:
*  void
*
* Return:
*  void
*
*******************************************************************************/
void start_radar_processing_task(void)
{
    if (xTaskCreate(processing_task, PROCESSING_TASK_NAME, PROCESSING_TASK_STACK_SIZE, NULL, PROCESSING_TASK_PRIORITY, &radar_processing_tsk_hdlr) != pdPASS)
    {
        LOG_ERROR(CYLF_DEF, "Radar start_radar_processing_task create failed.\n");
    }
}

/*******************************************************************************
* Function Name: start_radar_accquisition_task
********************************************************************************
* Summary:
*  Creates the radar data acquisition FreeRTOS task.
*
* Parameters:
*  void
*
* Return:
*  void
*
*******************************************************************************/
void start_radar_accquisition_task(void)
{
    if (xTaskCreate(accquisition_task, ACCQUISITION_TASK_NAME, ACCQUISITION_TASK_STACK_SIZE, NULL, ACCQUISITION_TASK_PRIORITY, &radar_accqusition_tsk_hdlr) != pdPASS)
    {
        LOG_ERROR(CYLF_DEF, "Radar start_radar_accquisition_task create failed.\n");
    }
}


/*******************************************************************************
* Function Name: accquisition_task
********************************************************************************
* Summary:
*  FreeRTOS task that acquires radar sensor data frames via SPI.
*
* Parameters:
*  pvParameters - Task parameters (unused).
*
* Return:
*  void
*
*******************************************************************************/
static void accquisition_task(void *pvParameters)
{
    (void)pvParameters;
    cy_rslt_t result = CY_RSLT_SUCCESS;
    uint32_t status = INIT_SUCCESS;
    static uint16_t samples[NUM_SAMPLES_PER_FRAME] = {0x00};
#if(ENABLE_RADAR_TIMING_LOG)
    TickType_t lastWakeTime = 0;
    TickType_t currentTime;
    TickType_t intervalTicks;
#endif

    /* Initialize radar sensor */
    if(CY_RSLT_SUCCESS != init_sensor())
    {
        LOG_ERROR(CYLF_DEF, "Radar sensor initialization failed.\n");
        vTaskSuspend(NULL);
    }
    else
    {
        LOG_INFO(CYLF_DEF, "Radar sensor initialization Ok.\n");
    }

    /* Start radar processing task */
    start_radar_processing_task();

    uint32_t frame_idx = 0;
    uint16_t test_word = XENSIV_BGT60TRXX_INITIAL_TEST_WORD;

    LOG_INFO(CYLF_DEF, "Radar accquisition_task start Ok\n");

    for(;;)
    {
        /* Wait for the GPIO interrupt to indicate that another slice is available */
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        if (xensiv_bgt60trxx_get_fifo_data(&sensor.dev, samples,
                                           NUM_SAMPLES_PER_FRAME) == XENSIV_BGT60TRXX_STATUS_OK)
        {
            /* Data pre-processing */
            uint16_t *bgt60_buffer_ptr = &samples[0];
            float32_t *frame_ptr = &frame[0];
            for (int32_t sample = 0; sample < NUM_SAMPLES_PER_FRAME; ++sample)
            {
                *frame_ptr++ = ((float32_t)(*bgt60_buffer_ptr++) / 4096.0F);
            }

            // calculate the average of the chirps first
            arm_fill_f32(0, avg_chirp, NUM_SAMPLES_PER_CHIRP);

            for (int chirp = 0; chirp < NUM_CHIRPS_PER_FRAME; chirp++)
            {
                arm_add_f32(avg_chirp, &frame[NUM_SAMPLES_PER_CHIRP * chirp], avg_chirp, NUM_SAMPLES_PER_CHIRP);
            }

            arm_scale_f32(avg_chirp, 1.0f / NUM_CHIRPS_PER_FRAME, avg_chirp, NUM_SAMPLES_PER_CHIRP);

            /* Tell processing task to take over */
            xTaskNotifyGive(radar_processing_tsk_hdlr);
        }

#if(ENABLE_RADAR_TIMING_LOG)
        /* Get current time in ticks */
        currentTime = xTaskGetTickCount();

        /* Skip calculation on first run */
        if (lastWakeTime != 0)
        {
            intervalTicks = currentTime - lastWakeTime;
            printf("Acquisition task intv: %lu ms\n", (unsigned long)pdTICKS_TO_MS(intervalTicks));
        }

        /* Store this time for next calculation */
        lastWakeTime = currentTime;
#endif

        frame_idx++;
    }

}

/*******************************************************************************
* Function Name: presence_detection_cb
********************************************************************************
* Summary:
*  Callback for radar presence detection state changes.
*
* Parameters:
*  handle - Radar presence handle.
*  event - Pointer to presence event data.
*  data - User data (unused).
*
* Return:
*  void
*
*******************************************************************************/
static void presence_detection_cb(xensiv_radar_presence_handle_t handle,
                           const xensiv_radar_presence_event_t* event,
                           void *data)
{
    (void)handle;
    (void)data;
    presence_status_t status = ABSENCE_DETECTED;
    static presence_status_t last_status = ABSENCE_DETECTED;

    switch (event->state)
    {
        case XENSIV_RADAR_PRESENCE_STATE_MACRO_PRESENCE:
            status = PRESENCE_DETECTED;
            break;

        case XENSIV_RADAR_PRESENCE_STATE_MICRO_PRESENCE:
            status = PRESENCE_DETECTED;
            break;

        case XENSIV_RADAR_PRESENCE_STATE_ABSENCE:
            status = ABSENCE_DETECTED;
            break;

        default:
            LOG_ERROR(CYLF_DEF, "Unknown reported state in event handling\n\r");
            break;
    }
    
    /* Update detection state if changed */
    if (last_status != status)
    {
        update_presence_detection_status(status);
        last_status = status;
    }
}

/*******************************************************************************
* Function Name: processing_task
********************************************************************************
* Summary:
*  FreeRTOS task that processes radar frame data for presence detection.
*
* Parameters:
*  pvParameters - Task parameters (unused).
*
* Return:
*  void
*
*******************************************************************************/
static void processing_task(void *pvParameters)
{
    (void)pvParameters;
    uint32_t pr_frame_idx = 0;

#if(ENABLE_RADAR_TIMING_LOG)
    TickType_t lastWakeTime = 0;
    TickType_t currentTime;
    TickType_t intervalTicks;
#endif

    xensiv_radar_presence_handle_t handle;
    static const xensiv_radar_presence_config_t default_config =
    {
        .bandwidth                         = 460E6,
        .num_samples_per_chirp             = XENSIV_BGT60TRXX_CONF_NUM_SAMPLES_PER_CHIRP,
        .micro_fft_decimation_enabled      = false,
        .micro_fft_size                    = 128,
        .macro_threshold                   = 0.5f,
        .micro_threshold                   = 12.5f,
        .min_range_bin                     = 1,
        .max_range_bin                     = 3,
        .macro_compare_interval_ms         = 25,       //250
        .macro_movement_validity_ms        = 100,          //1000
        .micro_movement_validity_ms        = 250,          //4000
        .macro_movement_confirmations      = 0,
        .macro_trigger_range               = 1,
        .mode                              = XENSIV_RADAR_PRESENCE_MODE_MICRO_IF_MACRO,
        .macro_fft_bandpass_filter_enabled = false,
        .micro_movement_compare_idx       = 5
    };
    
    xensiv_radar_presence_set_malloc_free(pvPortMalloc,
                                          vPortFree);

    if (xensiv_radar_presence_alloc(&handle, &default_config) != 0)
    {
        CY_ASSERT(0);
    }

    /* Set callback function to trigger on presence detection */
    xensiv_radar_presence_set_callback(handle, presence_detection_cb, NULL);

    LOG_INFO(CYLF_DEF, "Radar processing_task start Ok\n");

    for(;;)
    {
        /* Wait for frame data available to process */
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        if(XENSIV_RADAR_PRESENCE_OK != xensiv_radar_presence_process_frame(handle, frame, xTaskGetTickCount() * portTICK_PERIOD_MS))
        {
            LOG_ERROR(CYLF_DEF, "Error xensiv_radar_presence_process_frame\n");
        }
        else
        {
#if(ENABLE_RADAR_TIMING_LOG)
        /* Get current time in ticks */
        currentTime = xTaskGetTickCount();

        /* Skip calculation on first run */
        if (lastWakeTime != 0)
        {
            intervalTicks = currentTime - lastWakeTime;
            printf("Processing task intv: %lu ms\n", (unsigned long)pdTICKS_TO_MS(intervalTicks));
        }

        /* Store this time for next calculation */
        lastWakeTime = currentTime;
#endif

            pr_frame_idx++;
        }
    }
}

/*******************************************************************************
* Function Name: xensiv_bgt60trxx_mtb_interrupt_handler
********************************************************************************
* Summary:
*  GPIO interrupt handler for radar sensor data-ready signal.
*
* Parameters:
*  void
*
* Return:
*  void
*
*******************************************************************************/
static void xensiv_bgt60trxx_mtb_interrupt_handler(void)
{
    /* Clears the triggered pin interrupt */
    Cy_GPIO_ClearInterrupt(CYBSP_RADAR_INT_PORT, CYBSP_RADAR_INT_NUM);
    NVIC_ClearPendingIRQ(irq_cfg.intrSrc);

    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    vTaskNotifyGiveFromISR(radar_accqusition_tsk_hdlr, &xHigherPriorityTaskWoken);
    /* Context switch needed */
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

