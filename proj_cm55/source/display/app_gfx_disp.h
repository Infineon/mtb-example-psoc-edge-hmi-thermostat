/*******************************************************************************
 * File Name:  app_gfx_disp.h
 *
 * Description: Public interface for the GFX display task routines.
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

#ifndef APP_GFX_DISP_H_
#define APP_GFX_DISP_H_


#ifdef __cplusplus
extern "C" {
#endif
/*******************************************************************************
 *                                INCLUDES
 *******************************************************************************/
#include "cybsp.h"
#include "retarget_io_init.h"
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "queue.h"
#include "cyabs_rtos.h"
#include "cyabs_rtos_impl.h"
#include "lvgl.h"
#include "ipc_communication.h"

/*******************************************************************************
 *                                CONSTANTS
 *******************************************************************************/
#define RESET_VAL                           (0U)

/*******************************************************************************
 *                               GLOBAL VARIABLES
 *******************************************************************************/
extern lv_display_t * disp;
extern TaskHandle_t rtos_cm55_gfx_task_handle;
extern char *intent_text;
extern QueueHandle_t cm55_ipc_rx_queue;
extern volatile uint32_t cm55_ipc_rx_drop_count;
extern mtb_hal_i2c_t CYBSP_I2C_CONTROLLER_hal_obj;

/*******************************************************************************
 *                              FUNCTION PROTOTYPES
 *******************************************************************************/
void cm55_gfx_task(void *arg);

#ifdef __cplusplus
}
#endif

#endif /* APP_GFX_DISP_H_ */

/* [] END OF FILE */
