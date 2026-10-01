/*******************************************************************************
 * File Name:   app_bt_event_handler.h
 *
 * Description: Public interface for the Bluetooth management event handling,
 *              BLE stack init/deinit and advertisement control implemented in
 *              app_bt_event_handler.c.
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

#ifndef APP_BT_EVENT_HANDLER_H_
#define APP_BT_EVENT_HANDLER_H_


#ifdef __cplusplus
extern "C" {
#endif
/*******************************************************************************
 *                                INCLUDES
 *****************************************************************************/
#include "cybsp.h"
#include "wiced_bt_dev.h"
#include "wiced_bt_ble.h"
#include "wiced_bt_stack.h"
#include "cycfg_bt_settings.h"
#include "cycfg_gap.h"
#include "cybsp_bt_config.h"

/*******************************************************************************
 *                              GLOBAL VARIABLES
 *****************************************************************************/
/* BLE device name, derived from the Wi-Fi MAC address */
extern uint8_t ble_name[MAX_LEN_GAP_DEVICE_NAME+1];

/* Tracks BLE reconnection attempts */
extern uint8_t ble_retry_attempt;

/* Set when low duty-cycle advertising was requested manually, to avoid
 * re-triggering the low-advertising connectivity state */
extern bool is_manual_low_adv;

/*******************************************************************************
 *                          FUNCTION PROTOTYPES
 *****************************************************************************/
cy_rslt_t ble_init(void);
cy_rslt_t ble_deinit(void);
void ble_disconnect(void);
void ble_stop_advertising(void);
wiced_result_t ble_start_advertising(wiced_bt_ble_advert_mode_t mode);

#ifdef __cplusplus
}
#endif

#endif /* APP_BT_EVENT_HANDLER_H_ */
