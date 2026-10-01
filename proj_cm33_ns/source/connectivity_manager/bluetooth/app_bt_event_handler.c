/*******************************************************************************
 * File Name:   app_bt_event_handler.c
 *
 * Description: Implementation of the Bluetooth management event handling
 *              (BTM callback, pairing, bonding), BLE stack init/deinit and
 *              advertisement control.
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
#include "app_bt_event_handler.h"
#include "app_bt_gatt_handler.h"
#include "app_bt_bonding.h"
#include "app_bt_utils.h"
#include "wireless_manager.h"
#include "comm_manager.h"
#include "app_ui_receiver.h"
#include "wifi_task.h"
#include "cycfg_gatt_db.h"

/*******************************************************************************
 *                                MACROS
 *****************************************************************************/
/* LE Key Size */
#define MAX_KEY_SIZE (16U)

/* WICED Heap size for app */
#define APP_HEAP_SIZE              (0x1000U)

/* Macros to store the local BDA*/
#define LOCAL_BDA_1                  (0x00U)
#define LOCAL_BDA_2                  (0xA0U)
#define LOCAL_BDA_3                  (0x50U)
#define LOCAL_BDA_4                  (0x11U)
#define LOCAL_BDA_5                  (0x44U)
#define LOCAL_BDA_6                  (0x55U)
#define BOND_INDEX_INIT_VALUE        (0U)
#define CCCD_INDEX                   (0U)

/*******************************************************************************
 *                              GLOBAL VARIABLES
 *****************************************************************************/
/* Pointer to the heap created */
wiced_bt_heap_t * app_heap_pointer;

uint8_t ble_name[MAX_LEN_GAP_DEVICE_NAME+1] = {0};

static bool ble_stack_init_state = false;
uint8_t ble_retry_attempt = 0;
bool is_manual_low_adv = false;

/* This is the index for the link keys, cccd and privacy mode of the host we are
 * currently bonded to */
uint8_t  bondindex = BOND_INDEX_INIT_VALUE;

/*****************************************************************************
 * Static Function Prototype
 *****************************************************************************/
static wiced_result_t app_management_callback(wiced_bt_management_evt_t event,
                                       wiced_bt_management_evt_data_t *p_event_data);
static void application_init(void);

/*****************************************************************************
 * Function Definitions
 *****************************************************************************/

/*******************************************************************************
* Function Name: app_management_callback
********************************************************************************
* Summary:
*  BLE management event handler callback function.
*
* Parameters:
*  event - The BLE management event type.
*  p_event_data - Pointer to event-associated data.
*
* Return:
*  wiced_result_t - Result of the event handling.
*
*******************************************************************************/
static wiced_result_t app_management_callback(wiced_bt_management_evt_t event,
                                       wiced_bt_management_evt_data_t *p_event_data)
{
    wiced_result_t result = WICED_BT_SUCCESS;
    cy_rslt_t rslt;
    wiced_bt_dev_encryption_status_t *p_status;
    wiced_bt_device_address_t local_bda = {LOCAL_BDA_1, LOCAL_BDA_2,LOCAL_BDA_3,
                                           LOCAL_BDA_4,LOCAL_BDA_5,LOCAL_BDA_6};

    LOG_DEBUG(CYLF_DEF, "Bluetooth Management Event: %s\n", get_btm_event_name(event));

    switch (event)
    {
        case BTM_ENABLED_EVT:

            /* Bluetooth is enabled */
            wiced_bt_set_local_bdaddr((uint8_t *)cy_bt_device_address, BLE_ADDR_PUBLIC);

            /* Read and print the BD address */
            wiced_bt_dev_read_local_addr(local_bda);
            LOG_INFO(CYLF_DEF, "Local Bluetooth Address: ");
            print_bd_address(local_bda);
            LOG_INFO(CYLF_DEF, "\n");

            application_init();
            break;

        case BTM_DISABLED_EVT:
            break;
            /* Print passkey to the screen so that the user can enter it. */
        case BTM_PASSKEY_NOTIFICATION_EVT:
            LOG_INFO(CYLF_DEF, "********************************************************\r\n");
            LOG_INFO(CYLF_DEF, "Passkey Notification\r\n");
            LOG_INFO(CYLF_DEF, "PassKey: %" PRIu32 "\r\n",
                    p_event_data->user_passkey_notification.passkey );
            LOG_INFO(CYLF_DEF, "***********************************************************\r\n");
            display_ble_pin(p_event_data->user_passkey_notification.passkey);
            break;

        case BTM_PAIRING_IO_CAPABILITIES_BLE_REQUEST_EVT:
        {
            wiced_bt_dev_ble_io_caps_req_t *pairing_io_caps = &(p_event_data->
                                           pairing_io_capabilities_ble_request);

            pairing_io_caps->local_io_cap = BTM_IO_CAPABILITIES_DISPLAY_ONLY;  //BTM_IO_CAPABILITIES_NONE;

            pairing_io_caps->oob_data = BTM_OOB_NONE;

            pairing_io_caps->auth_req = BTM_LE_AUTH_REQ_SC_BOND;

            pairing_io_caps->max_key_size = MAX_KEY_SIZE;

            pairing_io_caps->init_keys = BTM_LE_KEY_PENC | BTM_LE_KEY_PID |
                                        BTM_LE_KEY_PCSRK | BTM_LE_KEY_LENC;

            pairing_io_caps->resp_keys = BTM_LE_KEY_PENC | BTM_LE_KEY_PID |
                                        BTM_LE_KEY_PCSRK | BTM_LE_KEY_LENC;

            break;
        }

        case BTM_PAIRING_COMPLETE_EVT:
            if (WICED_SUCCESS == p_event_data->
                                pairing_complete.pairing_complete_info.ble.status)
            {
                LOG_INFO(CYLF_DEF, "Pairing Complete: SUCCESS\n");

                /* NOTE: slot data (NUM_BONDED / NEXT_FREE_INDEX) is updated in
                 * BTM_PAIRED_DEVICE_LINK_KEYS_UPDATE_EVT, which may fire after
                 * this event.  Do NOT call app_bt_update_slot_data() here. */

                handle_connectivity_state(STATE_BLE_CONNECTED);
            }
            else /* Pairing Failed */
            {
                LOG_INFO(CYLF_DEF, "Pairing Complete: FAILED\n");
            }
            break;

        case BTM_PAIRED_DEVICE_LINK_KEYS_UPDATE_EVT:
        {
            /* Paired Device Link Keys update */
            /* Determine if this is a new device BEFORE saving keys, while the
             * slot counters still reflect the current valid bond list. */
            bool is_new_bond = (BOND_INDEX_MAX == app_bt_find_device_in_nvm(
                    p_event_data->paired_device_link_keys_update.bd_addr));

            rslt = app_bt_save_device_link_keys(&
                    (p_event_data->paired_device_link_keys_update));

            if (CY_RSLT_SUCCESS == rslt)
            {
                /* Update slot counters only for genuinely new devices, and only
                 * after the keys have been written to NVM. */
                if (is_new_bond)
                {
                    rslt = app_bt_update_slot_data();
                    if (CY_RSLT_TYPE_ERROR == rslt)
                    {
                        printf("Error updating bond slot data!\n");
                    }
                }

                printf("Successfully Bonded to ");
                print_bd_address(p_event_data->
                        paired_device_link_keys_update.bd_addr);
            }
            else
            {
                printf("Failed to bond! \n");
            }

            break;
        }

        case BTM_PAIRED_DEVICE_LINK_KEYS_REQUEST_EVT:
            /* Paired Device Link Keys Request */
            printf("Paired Device Link keys Request Event for device ");
            print_bd_address((uint8_t *)(p_event_data->
                    paired_device_link_keys_request.bd_addr));

            /* Need to search to see if the BD_ADDR
             * is in NVM. If not, return WICED_BT_ERROR
             * and the stack will generate keys and will then call
             * BTM_PAIRED_DEVICE_LINK_KEYS_UPDATE_EVT so that they
             * can be stored.
             */

            /* Assume the device won't be found.
             * If it is, set this back to WICED_BT_SUCCESS */
            result = WICED_BT_ERROR;
            bondindex = app_bt_find_device_in_nvm(p_event_data->
                    paired_device_link_keys_request.bd_addr);

            if(BOND_INDEX_MAX > bondindex)
            {
                /* Copy the keys to where the stack wants it */
                memcpy(&(p_event_data->paired_device_link_keys_request),
                       &bond_info.link_keys[bondindex],
                       sizeof(wiced_bt_device_link_keys_t));
                result = WICED_BT_SUCCESS;
            }
            else
            {
                printf("Device Link Keys not found in the database! \n");
            }

            break;

        case BTM_LOCAL_IDENTITY_KEYS_UPDATE_EVT:
            /* Update of local privacy keys - save to NVM */
            rslt = app_bt_save_local_identity_key(p_event_data->
                    local_identity_keys_update);
            if (CY_RSLT_SUCCESS != rslt)
            {
                result = WICED_BT_ERROR;
            }

            break;

        case BTM_LOCAL_IDENTITY_KEYS_REQUEST_EVT:
            /* Read Local Identity Resolution Keys if present in NVM*/
            rslt = app_bt_read_local_identity_keys();

            if(CY_RSLT_SUCCESS == rslt)
            {
                memcpy(&(p_event_data->local_identity_keys_request),
                       &(identity_keys), sizeof(wiced_bt_local_identity_keys_t));
                // print_array(&identity_keys, sizeof(wiced_bt_local_identity_keys_t));
                result = WICED_BT_SUCCESS;
            }
            else
            {
               result = WICED_BT_ERROR;
            }

            break;

        case BTM_ENCRYPTION_STATUS_EVT:
            p_status = &p_event_data->encryption_status;
            printf("Encryption status changed, BDA:");
            print_bd_address(p_status->bd_addr);

            /* Check and retreive the index of the bond data of the device that
             * got connected */
            /* This call will return BOND_INDEX_MAX if the device is not found */
            bondindex = app_bt_find_device_in_nvm(p_event_data->
                    encryption_status.bd_addr);
            if(bondindex < BOND_INDEX_MAX)
            {
                app_bt_restore_bond_data();
                app_bt_restore_cccd();

                /* Set CCCD value from the value that was previously saved in
                 * the NVM */
                // app_hello_sensor_notify_client_char_config[CCCD_INDEX] =
                //         peer_cccd_data[bondindex];
                printf("Bond info present in NVM for device: ");
                print_bd_address(p_event_data->encryption_status.bd_addr);
            }
            else
            {
                printf("No Bond info present in NVM for device: ");
                print_bd_address(p_event_data->encryption_status.bd_addr);
                bondindex=BOND_INDEX_INIT_VALUE;
            }

            /* Encryption completes AFTER the passkey display (new pair) and is
             * the only "connected" signal on a bonded reconnect; re-declare
             * connected here so the CM55 passkey window closes and the BLE
             * connected icon is shown. */
            if (WICED_SUCCESS == p_status->result)
            {
                handle_connectivity_state(STATE_BLE_CONNECTED);
            }

            break;

        case BTM_SECURITY_REQUEST_EVT:
            wiced_bt_ble_security_grant(p_event_data->security_request.bd_addr,
                                        WICED_BT_SUCCESS);
            break;

        case BTM_BLE_ADVERT_STATE_CHANGED_EVT:
            LOG_INFO(CYLF_DEF, "Advertisement state changed to %s\n", get_bt_advert_mode_name(
                                           p_event_data->ble_advert_state_changed));

            if(p_event_data->ble_advert_state_changed == BTM_BLE_ADVERT_OFF)
            {
                /* Do Nothing */
            }
            else if(p_event_data->ble_advert_state_changed == BTM_BLE_ADVERT_UNDIRECTED_LOW)
            {
                if(is_manual_low_adv != true)
                {
                    handle_connectivity_state(STATE_BLE_ACTIVE_LOW_MODE);
                }
                else
                {
                    is_manual_low_adv = false;
                }
                update_ui_connectivity_status();
            }
            else if(p_event_data->ble_advert_state_changed == BTM_BLE_ADVERT_UNDIRECTED_HIGH)
            {
                handle_connectivity_state(STATE_BLE_ACTIVE_HIGH_MODE);
            }
            break;

        default:
            break;
        }

    return result;
}

/*******************************************************************************
* Function Name: application_init
********************************************************************************
* Summary:
* This function is called from the BTM enabled event
*    1. Initializes and registers the Generic ATTribute Data Base (GATT DB)
*    2. Configures the USER button 1 for interrupt
*    3. Sets pairable mode to true
*    4. Sets ADV data and starts advertising
*
*******************************************************************************/
static void application_init(void)
{
    wiced_result_t result = WICED_ERROR;
    wiced_bt_gatt_status_t gatt_status = WICED_BT_GATT_INVALID_CONNECTION_ID;

    /* Create a buffer heap, make it the default heap */
    app_heap_pointer = wiced_bt_create_heap("app", NULL, APP_HEAP_SIZE, NULL,
                                            WICED_TRUE);
    if(NULL == app_heap_pointer)
    {
        LOG_ERROR(CYLF_DEF, "Failed to create heap\n");
        ERROR_CHECK(1);
    }

    if(CY_RSLT_SUCCESS == app_bt_restore_bond_data())
    {
        printf("Keys found in NVM, add them to Addr Res DB\n");

        /* Load previous paired keys for address resolution */
        app_bt_add_devices_to_address_resolution_db();
    }

    /* Register with stack to receive GATT callback */
    gatt_status = wiced_bt_gatt_register(app_gatts_callback);
    if(WICED_BT_GATT_SUCCESS != gatt_status)
    {
        LOG_ERROR(CYLF_DEF, "GATT register failed. Status: %s\n", get_bt_gatt_status_name(
                                                       gatt_status));
    }

    /*  Inform the stack to use our GATT database */
    gatt_status = wiced_bt_gatt_db_init(gatt_database, gatt_database_len, NULL);
    if(WICED_BT_GATT_SUCCESS != gatt_status)
    {
        LOG_ERROR(CYLF_DEF, "GATT db init failed. Status :%s\n",get_bt_gatt_status_name(
                                                     gatt_status));
    }

    /* Create the provisioning data sender task */
    if (pdPASS != xTaskCreate(prov_data_task, "ProvData", 2048U, NULL, 1U,
                              &prov_data_task_handle))
    {
        LOG_ERROR(CYLF_DEF, "Failed to create prov_data_task\n");
    }

    /* Allow peer to pair */
    wiced_bt_set_pairable_mode(WICED_TRUE, false);

    /* Set BLE advertisement data */
    result = wiced_bt_ble_set_raw_advertisement_data(CY_BT_ADV_PACKET_DATA_SIZE,
                                                     cy_bt_adv_packet_data);
    if (WICED_SUCCESS != result)
    {
        LOG_ERROR(CYLF_DEF, "Set ADV data failed\n");
    }

    wiced_bt_device_address_t ble_mac_addr = {0};

    /* First byte fixed as 0xC0 (static random requirement) */
    ble_mac_addr[0] = 0xC0;

    ble_mac_addr[1] = 0x04;

    /* Update BLE MAC Addr last 3 bytes from Wi-Fi MAC */
    for (int i = 0; i < 3; i++)
    {
        ble_mac_addr[i + 3] = wifi_mac[3 + i];
    }

    /* Set BLE Random Address */
    result = wiced_bt_set_local_bdaddr(ble_mac_addr, BLE_ADDR_RANDOM);
    if (WICED_BT_SUCCESS == result)
    {
        LOG_INFO(CYLF_DEF, "Bluetooth MAC Address Set Successfully: %02X:%02X:%02X:%02X:%02X:%02X\n",
               ble_mac_addr[0], ble_mac_addr[1], ble_mac_addr[2],
               ble_mac_addr[3], ble_mac_addr[4], ble_mac_addr[5]);
    }
    else
    {
        LOG_ERROR(CYLF_DEF, "Failed to Set Bluetooth MAC Address, Error Code: %d\n", result);
    }

    /* Read data from NVM if present */
    result =  Cy_RRAM_NvmReadByteArray(RRAMC0,
            APP_RRAM_NVM_MAIN_NS_START + RRAM_NVM_WIFI_CRED_OFFSET,
            ( uint8_t *)&wifi_cred.wifi_ssid[0], sizeof(wifi_cred) );

    if ((!result) && (wifi_cred.ssid_len))
    {
        LOG_INFO(CYLF_DEF, "Device provisioned, WiFi credentials present in NVM\n");

        /* Set the WiFi Connection parameters structure to 0 before copying data */
            memset(&wifi_conn_param, 0, sizeof(cy_wcm_connect_params_t));

        /* Copy the WiFi credentials to the global variable */
        memcpy(wifi_conn_param.ap_credentials.SSID, &wifi_cred.wifi_ssid[0], wifi_cred.ssid_len);
        memcpy(wifi_conn_param.ap_credentials.password, &wifi_cred.wifi_password[0], wifi_cred.password_len);

            memset(&wifi_details, 0, sizeof(wifi_details_t));
            memcpy(&wifi_details, &wifi_cred, sizeof(wifi_details_t));

            is_wifi_cred_need_to_update = true;

        handle_connectivity_state(STATE_PROVISIONED);

            xTaskNotify(wifi_task_handle, (NOTIF_SCAN | NOTIF_CONNECT), eSetValueWithOverwrite);
    }
    else /* WiFi credentials not found in NVM */
    {
        LOG_INFO(CYLF_DEF, "WiFi credentials not present in NVM\n");

    }

      ble_start_advertising(BTM_BLE_ADVERT_UNDIRECTED_HIGH);
}

/*******************************************************************************
* Function Name: ble_init
********************************************************************************
* Summary:
*  Initializes the Bluetooth stack with the management callback.
*
* Parameters:
*  void
*
* Return:
*  cy_rslt_t - CY_RSLT_SUCCESS on success.
*
*******************************************************************************/
cy_rslt_t ble_init(void)
{
    cy_rslt_t result = CY_RSLT_SUCCESS;

    if(false == ble_stack_init_state)
    {
        /* Initialize the Bluetooth stack with a callback function and stack
         * configuration structure */
        result = wiced_bt_stack_init(app_management_callback, &wiced_bt_cfg_settings);
        if (WICED_SUCCESS != result )
        {
            LOG_ERROR(CYLF_DEF, "Error in initializing the BT stack with error: %u\n", result);
        }
        else
        {
            LOG_INFO(CYLF_DEF, "BT stack initialized!\n");
            ble_stack_init_state = true;
        }
    }
    else
    {
        LOG_INFO(CYLF_DEF, "BT stack already initialized!\n");
    }

    return result;
}

/*******************************************************************************
* Function Name: ble_deinit
********************************************************************************
* Summary:
*  Deinitializes the Bluetooth stack.
*
* Parameters:
*  void
*
* Return:
*  cy_rslt_t - CY_RSLT_SUCCESS on success.
*
*******************************************************************************/
cy_rslt_t ble_deinit(void)
{
    cy_rslt_t result = CY_RSLT_SUCCESS;

    if(true == ble_stack_init_state)
    {
        /* Initialize the Bluetooth stack with a callback function and stack
         * configuration structure */
        result = wiced_bt_stack_deinit();
        if (WICED_SUCCESS != result )
        {
            LOG_ERROR(CYLF_DEF, "Error in deinitializing the BT stack with error: %u\n", result);
        }
        else
        {
            ble_stack_init_state = false;
        }
    }

    return result;
}


/*******************************************************************************
* Function Name: ble_disconnect
********************************************************************************
* Summary:
*  Disconnects BLE and removes bonding information.
*
* Parameters:
*  void
*
* Return:
*  void
*
*******************************************************************************/
void ble_disconnect(void)
{
    if(true == ble_stack_init_state)
    {
        ble_stop_advertising();
        vTaskDelay(100);

        wiced_bt_gatt_disconnect(conn_id);
        vTaskDelay(100);

        /* Delete the bonding information for the remote device */
        wiced_result_t result = wiced_bt_dev_delete_bonded_device(peer_addr);
        if (WICED_SUCCESS == result)
        {
            LOG_INFO(CYLF_DEF, "Bonding information for device %02X:%02X:%02X:%02X:%02X:%02X removed successfully.\n", peer_addr[0],
                    peer_addr[1], peer_addr[2], peer_addr[3], peer_addr[4], peer_addr[5]);
        }
        else
        {
            LOG_INFO(CYLF_DEF, "Failed to remove bonding information for device %02X:%02X:%02X:%02X:%02X:%02X\n", peer_addr[0],
                    peer_addr[1], peer_addr[2], peer_addr[3], peer_addr[4], peer_addr[5]);
        }
        vTaskDelay(100);
    }
}

/*******************************************************************************
* Function Name: ble_start_advertising
********************************************************************************
* Summary:
*  Starts BLE advertising in the specified mode.
*
* Parameters:
*  mode - The BLE advertising mode to use.
*
* Return:
*  wiced_result_t - WICED_SUCCESS on success.
*
*******************************************************************************/
wiced_result_t ble_start_advertising(wiced_bt_ble_advert_mode_t mode)
{
    if(mode == wiced_bt_ble_get_current_advert_mode() || is_switch_to_wifi_enabled())
    {
        /* Return if an identical ADV exists or if Wi-Fi connection is forced */
        if(is_switch_to_wifi_enabled())
        {
            LOG_ERROR(CYLF_DEF, "No need start ADV as \"Switch to wifi\" is enabled\r\n");
        }
        else
        {
            /* Already advertising - report it so CM55 isn't left waiting for a state update */
            LOG_ERROR(CYLF_DEF, "ADV does not need to be started again because it is already active\r\n");
            update_conn_state(DEV_ST_BLE_ADVERTISING);
        }
        return WICED_SUCCESS;
    }

    if(!conn_id)
    {
        ble_stop_advertising();
        vTaskDelay(50);

        if(WICED_SUCCESS != wiced_bt_ble_set_raw_advertisement_data(CY_BT_ADV_PACKET_DATA_SIZE,
                                                                                   cy_bt_adv_packet_data))
        {
            LOG_ERROR(CYLF_DEF, "Set ADV data failed\n");
        }

        wiced_result_t adv_result = wiced_bt_start_advertisements(mode, false, NULL);
        if(WICED_SUCCESS == adv_result)
        {
            update_conn_state(DEV_ST_BLE_ADVERTISING);
        }
        return adv_result;
    }
    else
    {
        update_conn_state(DEV_ST_BLE_CONNECTED);
    }

    return WICED_SUCCESS;
}

/*******************************************************************************
* Function Name: ble_stop_advertising
********************************************************************************
* Summary:
*  Stops BLE advertising.
*
* Parameters:
*  void
*
* Return:
*  void
*
*******************************************************************************/
void ble_stop_advertising(void)
{
    if(BTM_BLE_ADVERT_OFF != wiced_bt_ble_get_current_advert_mode())
    {
        
        wiced_bt_start_advertisements(BTM_BLE_ADVERT_OFF, false, NULL);
    }
}

/* [] END OF FILE */
