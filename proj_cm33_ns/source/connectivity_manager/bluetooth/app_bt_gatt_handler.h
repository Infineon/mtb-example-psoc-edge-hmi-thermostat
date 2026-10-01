/*******************************************************************************
 * File Name:   app_bt_gatt_handler.h
 *
 * Description: Public interface for the GATT server event handling and BLE
 *              provisioning data transfer implemented in app_bt_gatt_handler.c.
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

#ifndef APP_BT_GATT_HANDLER_H_
#define APP_BT_GATT_HANDLER_H_


#ifdef __cplusplus
extern "C" {
#endif
/*******************************************************************************
 *                                INCLUDES
 *****************************************************************************/
#include "wiced_bt_dev.h"
#include "wiced_bt_gatt.h"
#include "FreeRTOS.h"
#include "task.h"

/*******************************************************************************
 *                              GLOBAL VARIABLES
 *****************************************************************************/
/* Maintains the connection id of the current connection */
extern uint16_t conn_id;

/* Flag to check for GATT congestion */
extern uint8_t is_gatt_congested;

/* Bluetooth address of the currently connected peer device */
extern wiced_bt_device_address_t peer_addr;

/* Task handle for the BLE provisioning data sender task */
extern TaskHandle_t prov_data_task_handle;

/*******************************************************************************
 *                          FUNCTION PROTOTYPES
 *****************************************************************************/
wiced_bt_gatt_status_t app_gatts_callback(wiced_bt_gatt_evt_t event,
                                          wiced_bt_gatt_event_data_t *p_data);
void prov_data_task(void *pvParameters);
void app_bt_send_message(char *data, size_t data_len);
bool get_ble_connection_status(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_BT_GATT_HANDLER_H_ */
