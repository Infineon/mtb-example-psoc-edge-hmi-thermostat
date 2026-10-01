/*******************************************************************************
 * File Name:   wireless_manager.c
 *
 * Description: Implementation of the common connectivity interface
 *              coordinating BLE, Wi-Fi, and cloud connection state (see
 *              source/connectivity_manager/bluetooth and .../wifi for the
 *              protocol-specific implementations).
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
 *****************************************************************************/
#include "wireless_manager.h"
#include "app_ui_receiver.h"
#include "subscriber_task.h"
#include "comm_manager.h"
#include "mqtt_command_handler.h"

#include "app_bt_event_handler.h"
#include "app_bt_gatt_handler.h"

/*******************************************************************************
 *                              GLOBAL VARIABLES
 *****************************************************************************/
bool cloud_connection_inprogress = false;
bool provision_inprogress = false;
bool device_provisioned = false;
static connectivity_state_t current_state         = STATE_UNPROVISIONED;
connection_retries_t wifi_auto_reconnect_retry     = WIFI_CONN_RETRY_LIMITED;
connection_retries_t cloud_auto_reconnect_retry = CLOUD_CONN_RETRY_LIMITED;
connection_retries_t ble_auto_reconnect_retry     = BLE_CONN_RETRY_LIMITED;

/*******************************************************************************
* Function Name: update_ui_connectivity_status
********************************************************************************
* Summary:
*  Updates the UI with the current device connectivity state.
*
* Parameters:
*  void
*
* Return:
*  void
*
*******************************************************************************/
void update_ui_connectivity_status(void)
{
    device_connection_state_t ui_state = DEV_ST_MAX;
    if(get_ble_connection_status())
    {
        ui_state = DEV_ST_BLE_CONNECTED;
    }
    else if((get_mqtt_status() & FLAG_MQTT_CONNECTION_SUCCESS))
    {
        ui_state = DEV_ST_CLOUD_CONNECTED;
    }
    else if(is_wifi_connected())
    {
        ui_state = DEV_ST_CLOUD_DISCONNECTED;
    }
    else
    {
        ble_start_advertising(BTM_BLE_ADVERT_UNDIRECTED_HIGH);
    }
    
    if(DEV_ST_MAX != ui_state)
    {
        update_conn_state(ui_state);    
    }
}

/*****************************************************************************
 * Function Definitions
 *****************************************************************************/

/*******************************************************************************
* Function Name: wirelessdevice_init
********************************************************************************
* Summary:
*  Initializes Wi-Fi and BLE wireless modules.
*
* Parameters:
*  void
*
* Return:
*  cy_rslt_t - CY_RSLT_SUCCESS on success, error code otherwise.
*
*******************************************************************************/
cy_rslt_t wirelessdevice_init(void)
{
    cy_rslt_t result = CY_RSLT_SUCCESS;

    result = wifi_init();
    if(CY_RSLT_SUCCESS != result)
    {
        LOG_ERROR(CYLF_DEF, "Unable to init. Wi-Fi module with error: %u\n", result);
        return result;
    }

    wifi_get_macaddr((uint8_t *)wifi_mac);

    result = ble_init();
    if(CY_RSLT_SUCCESS != result)
    {
        LOG_ERROR(CYLF_DEF, "Unable to init. BLE module with error: %u\n", result);
        return result;
    }

    /* Update the BLE name based on MAC address */
    snprintf((char *)ble_name, sizeof(ble_name),"Therm_%X%X%X", wifi_mac[3], wifi_mac[4], wifi_mac[5]);
    memcpy(app_gap_device_name, ble_name, MAX_LEN_GAP_DEVICE_NAME);
    printf("BLE Device Name: %s\n", ble_name);
    cy_bt_adv_packet_data[1].p_data = (uint8_t *)ble_name;
    cy_bt_scan_resp_packet_data[0].p_data = (uint8_t *)ble_name;

    setuid(wifi_mac[3], wifi_mac[4], wifi_mac[5]);

    return result;
}

/*******************************************************************************
* Function Name: handle_connectivity_state
********************************************************************************
* Summary:
*  State machine for handling connectivity state transitions.
*
* Parameters:
*  state - The connectivity state to transition to.
*
* Return:
*  void
*
*******************************************************************************/
void handle_connectivity_state(connectivity_state_t state)
{
    switch (state)
    {
        case STATE_PROVISIONED:
        {                    
            device_provisioned = true;
            update_provision_state(device_provisioned);
            ble_retry_attempt = 0;
            break;
        }

        case STATE_UNPROVISIONED:
        {
            device_provisioned = false;
            ble_retry_attempt = 0;
            update_conn_state(DEV_ST_UNPROVISIONED);
            update_comm_interface();
            break;
        }

        case STATE_START_PROVISIONING:
        {
            provision_inprogress = true;
            ble_retry_attempt = 0;
            update_conn_state(DEV_ST_BLE_ADVERTISING);
            ble_start_advertising(BTM_BLE_ADVERT_UNDIRECTED_HIGH);
            break;
        }

        case STATE_BLE_ACTIVE_HIGH_MODE:
        {
            /* Do Nothing */
            break;
        }

        case STATE_BLE_ACTIVE_LOW_MODE:
        {
            if((BLE_CONN_RETRY_INFINITE == ble_auto_reconnect_retry)/* || (MAX_CONNECTION_RETRIES > ble_retry_attempt++)*/)
            {
                ble_start_advertising(BTM_BLE_ADVERT_UNDIRECTED_HIGH);
            }
            else if(!device_provisioned)
            {
                update_conn_state(DEV_ST_UNPROVISIONED);
                ble_stop_advertising();
            }
            break;
        }

        case STATE_BLE_ADV:
        {
            /* Don't advertise if already connected */
            if(conn_id != false)
            {
                LOG_INFO(CYLF_DEF, "STATE_BLE_ADV Device already connected over BLE\n");
            }
            else
            {
                
                update_conn_state(DEV_ST_BLE_ADVERTISING);
                ble_start_advertising(BTM_BLE_ADVERT_UNDIRECTED_HIGH);
            }

            cloud_connection_inprogress = false;
            break;
        }

        case STATE_BLE_CONNECTED:
        {
            update_comm_interface();
            update_conn_state(DEV_ST_BLE_CONNECTED);
            is_manual_low_adv = false;
            cloud_connection_inprogress = false;
            ble_retry_attempt = 0;
            break;
        }

        case STATE_WIFI_CONNECTED:
        {
            provision_inprogress = false;
            break;
        }

        case STATE_WIFI_CONNECTING:
    {
        cloud_connection_inprogress = true;
        update_conn_state(DEV_ST_WIFI_CONNECTING);
        break;
    }

        case STATE_WIFI_DISCONNECTED:
        {            
            update_ui_connectivity_status();
            break;
        }

        case STATE_CLOUD_CONNECT:
        {            
            update_comm_interface();
            update_ui_connectivity_status();
            vTaskDelay(100);
            
            is_manual_low_adv = true;
            cloud_connection_inprogress = false;
            
            ble_stop_advertising();
            vTaskDelay(500);
            
            /* Send the connection status to end device */
            for (int retry = 0; retry < 1; retry++)
            {
                send_response_numeric(DEVICE_STATUS, OPERATION_READ, 1);
                vTaskDelay(200);
            }

            if(!is_switch_to_wifi_enabled())
            {
                wiced_bt_start_advertisements(BTM_BLE_ADVERT_UNDIRECTED_LOW, false, NULL);
            }
            break;
        }

        case STATE_CLOUD_DISCONNECT:
        {
            cloud_connection_inprogress = false;
            update_ui_connectivity_status();

            /* Let the app know connect failed instead of leaving it waiting. */
            update_comm_interface();
            send_response_numeric(DEVICE_STATUS, OPERATION_READ, 0);
            break;
        }

        default:
            /* Handle unexpected states */
            break;
    }
    current_state = state;
}

/*******************************************************************************
* Function Name: get_device_provision_state
********************************************************************************
* Summary:
*  Returns the device provisioning status.
*
* Parameters:
*  void
*
* Return:
*  bool - true if device is provisioned.
*
*******************************************************************************/
bool get_device_provision_state(void)
{
    return device_provisioned;
}

/*******************************************************************************
* Function Name: get_retry_state
********************************************************************************
* Summary:
*  Gets the retry configuration for the given connectivity interface.
*
* Parameters:
*  interface - The connectivity interface to query.
*
* Return:
*  connection_retries_t - The retry state for the interface.
*
*******************************************************************************/
connection_retries_t get_retry_state(connectivity_interface_t interface)
{
    switch (interface)
    {
        case BLE_INTERFACE:
            /* BLE usually has limited retries */
            return ble_auto_reconnect_retry;

        case WIFI_INTERFACE:
            /* Wi-Fi may need more retries for stability */
            return wifi_auto_reconnect_retry;

        case CLOUD_INTERFACE:
            /* MQTT often requires persistent retries */
            return cloud_auto_reconnect_retry;

        case MAX_INTERFACE:
        default:
            /* Invalid interface, no retries */
            return MAX_CONN_RETRY;
    }
}

/*******************************************************************************
* Function Name: set_retry_state
********************************************************************************
* Summary:
*  Sets the retry configuration for the given connectivity interface.
*
* Parameters:
*  interface - The connectivity interface to configure.
*  retry_state - The retry state to set.
*
* Return:
*  void
*
*******************************************************************************/
void set_retry_state(connectivity_interface_t interface, connection_retries_t retry_state)
{
    switch (interface)
    {
        case BLE_INTERFACE:
            ble_auto_reconnect_retry = retry_state;
            break;

        case WIFI_INTERFACE:
            wifi_auto_reconnect_retry = retry_state;
            break;

        case CLOUD_INTERFACE:
            cloud_auto_reconnect_retry = retry_state;
            break;

        case MAX_INTERFACE:
        default:
            printf("Error: Invalid connectivity interface (%d)\r\n", interface);
            break;
    }
}
