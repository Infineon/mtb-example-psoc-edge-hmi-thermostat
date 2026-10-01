/*******************************************************************************
 * File Name:  app_sensor.c
 *
 * Description: Sensor control module implementation.
 *
 * This file contains the implementation for environmental sensor interfacing
 * and handling.
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
#include "app_sensor.h"
#include "timers.h"
#include "lv_timer.h"
#include "app_common.h"

/*******************************************************************************
 *                                MACROS
 ******************************************************************************/
#define RH_MIN_MILLI            0          /* 0.000 %RH */
#define RH_MAX_MILLI            100000     /* 100.000 %RH */
#define RH_CHANGE_THRESH_MILLI  500        /* 0.5 %RH change threshold for UI update */

/*******************************************************************************
 *                             GLOBAL VARIABLES
 ******************************************************************************/
mtb_hal_i2c_t CYBSP_I2C_DISPLAY_CONTROLLER_hal_obj;
volatile bool sensor_data_available = false;
int32_t read_temperature, read_humidity = 0;
uint16_t pre_humidity = 0;
uint16_t rh_pct = 0;
TimerHandle_t restart_sensor_timer = NULL;
/*******************************************************************************
 *                             STATIC VARIABLES
 ******************************************************************************/
static uint32_t sensor_sampling_intv = SENSOR_SAMPLING_INTERVAL_ACTIVE;

/****************************************************************************
 *                              FUNCTION DECLARATIONS
 ***************************************************************************/


/*******************************************************************************
 *                              FUNCTION DEFINITIONS
 ******************************************************************************/

/*******************************************************************************
* Function Name: sht_sensor_init
********************************************************************************
* Summary:
*  Initializes the SHT40 temperature and humidity sensor over I2C.
*
* Parameters:
*  void
*
* Return:
*  cy_rslt_t - CY_RSLT_SUCCESS on success, error code on failure.
*
*******************************************************************************/
cy_rslt_t sht_sensor_init(void)
{
    cy_rslt_t result = CY_RSLT_SUCCESS;

    LOG_INFO(CYLF_DEF, "Initializing SHT40 sensor...\r\n");

    /* Initialize SHT40 sensor */
    result = mtb_sht4x_init(&CYBSP_I2C_DISPLAY_CONTROLLER_hal_obj, MTB_SHT40_ADDRESS_DEFAULT);
    if (CY_RSLT_SUCCESS != result)
    {
        LOG_ERROR(CYLF_DEF, "SHT40 sensor initialization failed!\r\n");
        return result;
    }

    LOG_INFO(CYLF_DEF, "SHT40 sensor initialized successfully.\r\n");
    return CY_RSLT_SUCCESS;
}

/*******************************************************************************
* Function Name: sensor_init
********************************************************************************
* Summary:
*  Initializes all sensors with retry mechanism (up to 5 retries).
*
* Parameters:
*  void
*
* Return:
*  cy_rslt_t - CY_RSLT_SUCCESS on success, error code on failure.
*
*******************************************************************************/
cy_rslt_t sensor_init(void)
{
    cy_rslt_t result;
    int retry = 0;
    const int max_retries = 5;

    /* Initialize SHT40 sensor with retry */
    for (retry = 0; retry < max_retries; retry++)
    {
        result = sht_sensor_init();
        if (result == CY_RSLT_SUCCESS)
        {
            break;
        }
        LOG_INFO(CYLF_DEF, "SHT40 sensor init failed, retrying... (%d/%d)\r\n", retry + 1, max_retries);
        vTaskDelay(pdMS_TO_TICKS(1000)); /* small delay before retry */
    }

    if (result != CY_RSLT_SUCCESS)
    {
        LOG_ERROR(CYLF_DEF, "SHT40 sensor initialization failed after retries!\r\n");
        return result;
    }

    LOG_INFO(CYLF_DEF, "All sensors initialized successfully.\r\n");
    return CY_RSLT_SUCCESS;
}

/*******************************************************************************
* Function Name: sensor_task
********************************************************************************
* Summary:
*  FreeRTOS task that periodically reads temperature and humidity from
*  the SHT40 sensor and flags when new data is available.
*
* Parameters:
*  arg - Task argument (unused).
*
* Return:
*  void
*
*******************************************************************************/
void sensor_task(void *arg)
{
    CY_UNUSED_PARAMETER(arg);
    cy_rslt_t result;

    LOG_INFO(CYLF_DEF, "Sensor task running.\\r\\n");

    /* Initialize all sensors */
    result = sensor_init();
    if (result != CY_RSLT_SUCCESS)
    {
        LOG_ERROR(CYLF_DEF, "Sensor initialization failed! Task halted.\r\n");
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    }

    /* Initial delay to allow other tasks to start */
    vTaskDelay(pdMS_TO_TICKS(1000));

    for (;;)
    {
        /* Wait the sampling interval; ulTaskNotifyTake returns 0 on timeout (no notification) */
        if (0 == ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(sensor_sampling_intv)))
        {
            /* Wait for sensor task to acquire the i2c bus */
            if (xSemaphoreTake(i2c_mutex, portMAX_DELAY) == pdTRUE)
            {
                /* Read temperature and humidity from SHT40 */
                cy_rslt_t result = mtb_sht4x_measure_low_precision(&CYBSP_I2C_DISPLAY_CONTROLLER_hal_obj,
                                                            &read_temperature, &read_humidity);
                if (result == CY_RSLT_SUCCESS)
                {
                    /* Clamp to sensor’s valid physical range */
                    if (read_humidity < RH_MIN_MILLI)   read_humidity = RH_MIN_MILLI;
                    if (read_humidity > RH_MAX_MILLI)   read_humidity = RH_MAX_MILLI;
    
                    /* Change it to percentage */
                    rh_pct = (uint16_t)((read_humidity + 500) / 1000);
                    if (rh_pct > 100) rh_pct = 100;
    
                    static int32_t prev_read_humidity = -1;
                    bool changed = false;
                    if (prev_read_humidity < 0 || (prev_read_humidity > read_humidity + RH_CHANGE_THRESH_MILLI) ||
                                            (read_humidity > prev_read_humidity + RH_CHANGE_THRESH_MILLI)) {
                        changed = true;
                        prev_read_humidity = read_humidity;
                    }
    
                    if (changed)
                    {
                        sensor_data_available = true;
                    }
                }
                else
                {
                    LOG_ERROR(CYLF_DEF, "SHT40 measurement failed. Resetting sensor...\r\n");
                    sht4x_soft_reset();
                }
    
                /* Release the mutex, allowing other tasks to use the bus */
                xSemaphoreGive(i2c_mutex);
            }
        }
    }
}

/*******************************************************************************
* Function Name: set_sensor_sampling_interval
********************************************************************************
* Summary:
*  Updates the sensor sampling interval and wakes the sensor task.
*
* Parameters:
*  interval_ms - Sampling interval in milliseconds.
*
* Return:
*  void
*
*******************************************************************************/
void set_sensor_sampling_interval(uint32_t interval_ms)
{
    sensor_sampling_intv = interval_ms;
    xTaskNotifyGive(rtos_cm55_sensor_task_handle);
}

/*******************************************************************************
* Function Name: app_sensor_task_init
********************************************************************************
* Summary:
*  Creates the sensor FreeRTOS task for periodic sensor reading.
*
* Parameters:
*  void
*
* Return:
*  void
*
*******************************************************************************/
void app_sensor_task_init(void)
{
    BaseType_t task_return = pdFAIL;

    /* Start sensor task */
    task_return = xTaskCreate(sensor_task, SENSOR_TASK_NAME,
                             SENSOR_TASK_STACK_SIZE, NULL,
                             SENSOR_TASK_PRIORITY,
                             &rtos_cm55_sensor_task_handle);

    if (task_return == pdFAIL)
    {
        LOG_ERROR(CYLF_DEF, "App. sensor task create failed.\r\n");
        APP_ERROR(1);
    }
    else
    {
        LOG_INFO(CYLF_DEF, "App. sensor task created successfully.\r\n");
    }
}

/*******************************************************************************
* Function Name: get_current_humidity_pct
********************************************************************************
* Summary:
*  Returns the current relative humidity as a percentage (0-100).
*
* Parameters:
*  void
*
* Return:
*  uint8_t - Humidity percentage.
*
*******************************************************************************/
uint8_t get_current_humidity_pct(void)
{
    return rh_pct;
}