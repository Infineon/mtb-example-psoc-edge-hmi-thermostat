/*******************************************************************************
 * File Name:   app_sensor.h
 *
 * Description:  Public interface for sensor control.
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

#ifndef APP_SENSOR_H_
#define APP_SENSOR_H_


#ifdef __cplusplus
extern "C" {
#endif
/*******************************************************************************
 *                                INCLUDES
 *******************************************************************************/
#include "cybsp.h"
#include "mtb_sht4x.h"
#include "retarget_io_init.h"
#include "FreeRTOS.h"
#include "task.h"
#include "cyabs_rtos.h"
#include "cyabs_rtos_impl.h"
#include "semphr.h"

/*******************************************************************************
 *                                MACROS
 *******************************************************************************/
#define SENSOR_TASK_NAME                ("SensorTask")
#define SENSOR_TASK_STACK_SIZE          (256U)
#define SENSOR_TASK_PRIORITY            (2U)

#define WAIT_SENSOR_RDY_MS              (1000)

#define DEFAULT_PRESSURE_REF_HPA        (0x3F7)

#define SENSOR_SAMPLING_INTERVAL_ACTIVE 2000U   /* Active state interval for sampling (2s) */
#define SENSOR_SAMPLING_INTERVAL_IDLE   10000U   /* Idle state interval for sampling (10s) */

/*******************************************************************************
 *                                CONSTANTS
 *******************************************************************************/

/*******************************************************************************
 *                                DATA TYPES
 *******************************************************************************/

/*******************************************************************************
 *                                GLOBAL VARIABLES
 *******************************************************************************/
extern mtb_hal_i2c_t CYBSP_I2C_DISPLAY_CONTROLLER_hal_obj;
extern volatile bool sensor_data_available;
extern int32_t read_temperature, read_humidity;
extern SemaphoreHandle_t i2c_mutex;
extern TimerHandle_t restart_sensor_timer;
extern TaskHandle_t rtos_cm55_sensor_task_handle;
extern cy_stc_scb_i2c_context_t disp_touch_i2c_controller_context;
extern uint16_t rh_pct;

/*******************************************************************************
 *                                FUNCTION PROTOTYPES
 *******************************************************************************/
cy_rslt_t sht_sensor_init(void);
cy_rslt_t sensor_init(void);
void sensor_task(void *arg);
void set_sensor_sampling_interval(uint32_t interval_ms);
void app_sensor_task_init(void);
uint8_t get_current_humidity_pct(void);
#ifdef __cplusplus
}
#endif

#endif /* APP_SENSOR_H_ */

/* [] END OF FILE */
