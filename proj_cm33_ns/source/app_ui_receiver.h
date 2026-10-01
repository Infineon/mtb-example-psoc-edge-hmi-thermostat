/*******************************************************************************
 * File Name:   app_ui_receiver.h
 *
 * Description:  Public interface for the UI receiver module.
 *
 * This file contains the headers and structures to support the UI receiver
 * task, which handles events and messages from various sources, including the
 * inter-processor communication (IPC) pipe.
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

#ifndef PROJ_CM33_NS_APP_INCLUDE_APP_UI_RECEIVER_H_
#define PROJ_CM33_NS_APP_INCLUDE_APP_UI_RECEIVER_H_


#ifdef __cplusplus
extern "C" {
#endif
/*******************************************************************************
 *                              INCLUDES
 *******************************************************************************/
#include "cybsp.h"
#include "FreeRTOS.h"
#include "task.h"
#include "cyabs_rtos.h"
#include "cyabs_rtos_impl.h"
#include "ipc_communication.h"
#include "app_common.h"

/*******************************************************************************
 *                              MACROS
 *******************************************************************************/
#define CY_RSLT_INVALID -1

/*******************************************************************************
 *                              GLOBAL VARIABLES
 *******************************************************************************/
extern QueueHandle_t xUiRxQueue;
extern TaskHandle_t cm33_ui_rx_task_handle;

/*******************************************************************************
 *                              FUNCTION PROTOTYPES
 *******************************************************************************/
void ui_rx_thread_init(void);
void get_latest_fw_version(void);
bool is_switch_to_wifi_enabled(void);
void set_wifi_switch_disabled(void);
#ifdef __cplusplus
}
#endif

#endif /* PROJ_CM33_NS_APP_INCLUDE_APP_UI_RECEIVER_H_ */

/* [] END OF FILE */
