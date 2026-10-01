/*******************************************************************************
 * File Name:   wireless_manager.h
 *
 * Description: Header file for the common connectivity interface
 *              coordinating BLE, Wi-Fi, and cloud connection state.
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

#ifndef WIRELESS_MANAGER_H_
#define WIRELESS_MANAGER_H_


#ifdef __cplusplus
extern "C" {
#endif
/*******************************************************************************
 *                                INCLUDES
 *****************************************************************************/
#include "retarget_io_init.h"
#include "FreeRTOS.h"
#include "task.h"
#include "cyabs_rtos.h"
#include "cyabs_rtos_impl.h"
#include "cycfg_peripherals.h"
#include <inttypes.h>
#include "cybsp.h"
#include "cy_time.h"
#include "cy_wcm.h"
#include "wifi_task.h"
#include "app_common.h"

/* Maximum number of connection retries for wireless network. */
#define MAX_CONNECTION_RETRIES            (5U)

typedef enum
{
    STATE_PROVISIONED,
    STATE_UNPROVISIONED,
    STATE_START_PROVISIONING,

    STATE_BLE_ACTIVE_HIGH_MODE,
    STATE_BLE_ACTIVE_LOW_MODE,
    STATE_BLE_ADV,
    STATE_BLE_CONNECTED,

    STATE_WIFI_CONNECTING,
    STATE_WIFI_CONNECTED,
    STATE_WIFI_DISCONNECTED,

    STATE_CLOUD_CONNECT,
    STATE_CLOUD_DISCONNECT,
} connectivity_state_t;


cy_rslt_t wirelessdevice_init(void);
bool get_device_provision_state();
void handle_connectivity_state(connectivity_state_t state);
void set_retry_state(connectivity_interface_t interface, connection_retries_t retry_state);
void update_ui_connectivity_status(void);
connection_retries_t get_retry_state(connectivity_interface_t interface);

extern bool device_provisioned;
extern bool is_wifi_cred_need_to_update;
extern bool is_wifi_connection_cancel;
extern bool is_cloud_connection_cancel;
extern bool update_credential;
extern ipc_msg_t *ipc_recv_msg;
extern char current_fw_version[MAX_FW_VERSION_LEN];


#ifdef __cplusplus
}
#endif

#endif /* WIRELESS_MANAGER_H_ */
