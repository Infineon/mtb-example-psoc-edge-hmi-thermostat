/*******************************************************************************
 * File Name:   app_bt_gatt_handler.c
 *
 * Description: Implementation of the GATT server event handling (reads,
 *              writes, connect/disconnect) and BLE credential provisioning
 *              data transfer.
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
#include "app_bt_gatt_handler.h"
#include "app_bt_event_handler.h"
#include "app_bt_bonding.h"
#include "app_bt_utils.h"
#include "wireless_manager.h"
#include "comm_manager.h"
#include "mqtt_command_handler.h"
#include "mqtt_client_config.h"
#include "mqtt_task.h"
#include "wifi_task.h"
#include "cycfg_gatt_db.h"
#include "cycfg_bt_settings.h"

/*******************************************************************************
 *                                MACROS
 *****************************************************************************/
#define INITIAL_LEN_FILLED         (0U)
#define CRED_INIT_VALUE            (0U)

/* BLE provisioning stream: type bytes */
#define PROV_TYPE_ENDPOINT         (0x01U)
#define PROV_TYPE_CLIENT_ID        (0x02U)
#define PROV_TYPE_CLIENT_CERT      (0x03U)
#define PROV_TYPE_PRIVATE_KEY      (0x04U)
#define PROV_TYPE_ROOT_CA          (0x05U)
#define PROV_TYPE_COMPLETE         (0xFFU)

/* Maximum payload bytes per BLE notification packet (70-byte MTU - 2 header bytes) */
#define PROV_CHUNK_MAX_LEN         (68U)

#define PROV_CHUNK_DELAY_MS        (20U)

/*******************************************************************************
 *                              DATA TYPES
 *****************************************************************************/
typedef void (*pfn_free_buffer_t)(uint8_t *);

/*******************************************************************************
 *                              GLOBAL VARIABLES
 *****************************************************************************/
/* Maintains the connection id of the current connection */
uint16_t conn_id = 0;
wiced_bt_device_address_t peer_addr;

/* This variable tracks the number of congestion events during BLE data transfer */
uint16_t num_of_congestions = 0;

/* Flag to check for GATT congestion */
uint8_t is_gatt_congested;

/* Task handle for the BLE provisioning data sender task */
TaskHandle_t prov_data_task_handle = NULL;

/*****************************************************************************
 * Static Function Prototype
 *****************************************************************************/
static void ble_send_provisioning_field(uint8_t type, const char *data, size_t total_len);
static void ble_send_provisioning_data(void);
static gatt_db_lookup_table_t *app_get_attribute(uint16_t handle);
static wiced_bt_gatt_status_t app_gatts_req_read_handler(uint16_t conn_id,
                                                  wiced_bt_gatt_opcode_t opcode,
                                                  wiced_bt_gatt_read_t *p_read_data,
                                                  uint16_t len_requested);
static wiced_bt_gatt_status_t app_gatt_read_by_type_handler(uint16_t conn_id,
                                         wiced_bt_gatt_opcode_t opcode,
                                         wiced_bt_gatt_read_by_type_t *p_read_data,
                                         uint16_t len_requested);
static wiced_bt_gatt_status_t app_gatts_req_write_handler(uint16_t conn_id,
                                                   wiced_bt_gatt_opcode_t opcode,
                                                   wiced_bt_gatt_write_req_t *p_data);
static wiced_bt_gatt_status_t app_gatt_connect_handler(
                       wiced_bt_gatt_connection_status_t *p_conn_status);
static wiced_bt_gatt_status_t app_gatts_attr_req_handler(wiced_bt_gatt_attribute_request_t *p_data);

/*****************************************************************************
 * Function Definitions
 *****************************************************************************/

/*******************************************************************************
* Function Name: ble_send_provisioning_field
********************************************************************************
* Summary:
*  Sends a single provisioning field over the PROV_STREAM notification
*  characteristic in chunks. Each notification: [type][isLast][data...]
*
* Parameters:
*  type      - Provisioning type byte (PROV_TYPE_*).
*  data      - Pointer to the UTF-8 / PEM string to send.
*  total_len - Total length of the data.
*
* Return:
*  void
*
*******************************************************************************/
static void ble_send_provisioning_field(uint8_t type, const char *data, size_t total_len)
{
    uint8_t pkt[2U + PROV_CHUNK_MAX_LEN];
    size_t  offset = 0U;
    uint32_t chunk_num = 0U;

    LOG_INFO(CYLF_DEF, "[PROV] Sending type=0x%02X total_len=%u\n", type, (unsigned)total_len);

    while (offset < total_len)
    {
        if (!conn_id)
        {
            LOG_ERROR(CYLF_DEF, "[PROV] BLE disconnected mid-transfer (type=0x%02X offset=%u)\n",
                      type, (unsigned)offset);
            break;
        }

        size_t chunk_len = total_len - offset;
        if (chunk_len > PROV_CHUNK_MAX_LEN)
        {
            chunk_len = PROV_CHUNK_MAX_LEN;
        }

        pkt[0] = type;
        pkt[1] = ((offset + chunk_len) >= total_len) ? 0x01U : 0x00U;
        memcpy(&pkt[2], data + offset, chunk_len);

        while (is_gatt_congested && conn_id);

        if (!conn_id)
        {
            LOG_ERROR(CYLF_DEF, "[PROV] BLE disconnected waiting for congestion (type=0x%02X)\n", type);
            break;
        }

        LOG_DEBUG(CYLF_DEF, "[PROV] type=0x%02X chunk=%lu offset=%u len=%u isLast=%u\n",
                  type, (unsigned long)chunk_num, (unsigned)offset,
                  (unsigned)chunk_len, pkt[1]);

        wiced_bt_gatt_status_t notif_status = wiced_bt_gatt_server_send_notification(conn_id,
                HDLC_CUSTOM_SERVICE_PROV_STREAM_VALUE,
                (uint16_t)(2U + chunk_len),
                pkt, NULL);

        if (WICED_BT_GATT_SUCCESS != notif_status)
        {
            LOG_ERROR(CYLF_DEF, "[PROV] Notification failed type=0x%02X chunk=%lu status=0x%X\n",
                      type, (unsigned long)chunk_num, notif_status);
        }

        offset += chunk_len;
        chunk_num++;
        vTaskDelay(pdMS_TO_TICKS(PROV_CHUNK_DELAY_MS));
    }

    LOG_INFO(CYLF_DEF, "[PROV] Done type=0x%02X sent %lu chunks\n", type, (unsigned long)chunk_num);
}
/*******************************************************************************
* Function Name: ble_send_provisioning_data
********************************************************************************
* Summary:
*  Sends all MQTT provisioning credentials over PROV_STREAM notifications.
*  Order: endpoint(0x01) -> clientId(0x02) -> cert(0x03) ->
*         key(0x04) -> rootCA(0x05) -> complete(0xFF).
*
* Parameters:
*  void
*
* Return:
*  void
*
*******************************************************************************/
static void ble_send_provisioning_data(void)
{
    if (!conn_id)
    {
        LOG_INFO(CYLF_DEF, "[PROV] Skipped: BLE not connected\n");
        return;
    }

    LOG_INFO(CYLF_DEF, "[PROV] Starting provisioning transfer\n");

    LOG_INFO(CYLF_DEF, "[PROV] [0x01] Endpoint: %s (%u bytes)\n",
             MQTT_BROKER_ADDRESS, (unsigned)strlen(MQTT_BROKER_ADDRESS));
    ble_send_provisioning_field(PROV_TYPE_ENDPOINT,    MQTT_BROKER_ADDRESS,    strlen(MQTT_BROKER_ADDRESS));

    LOG_INFO(CYLF_DEF, "[PROV] [0x02] Client ID: %s (%u bytes)\n",
             MQTT_CLIENT_IDENTIFIER, (unsigned)strlen(MQTT_CLIENT_IDENTIFIER));
    ble_send_provisioning_field(PROV_TYPE_CLIENT_ID,   MQTT_CLIENT_IDENTIFIER, strlen(MQTT_CLIENT_IDENTIFIER));

    LOG_INFO(CYLF_DEF, "[PROV] [0x03] Client Certificate (%u bytes)\n",
             (unsigned)strlen(CLIENT_CERTIFICATE));
    ble_send_provisioning_field(PROV_TYPE_CLIENT_CERT, CLIENT_CERTIFICATE,     strlen(CLIENT_CERTIFICATE));

    LOG_INFO(CYLF_DEF, "[PROV] [0x04] Private Key (%u bytes)\n",
             (unsigned)strlen(CLIENT_PRIVATE_KEY));
    ble_send_provisioning_field(PROV_TYPE_PRIVATE_KEY, CLIENT_PRIVATE_KEY,     strlen(CLIENT_PRIVATE_KEY));

    LOG_INFO(CYLF_DEF, "[PROV] [0x05] Root CA (%u bytes)\n",
             (unsigned)strlen(ROOT_CA_CERTIFICATE));
    ble_send_provisioning_field(PROV_TYPE_ROOT_CA,     ROOT_CA_CERTIFICATE,    strlen(ROOT_CA_CERTIFICATE));

    /* Transfer complete signal */
    if (conn_id)
    {
        uint8_t done_pkt[2] = { PROV_TYPE_COMPLETE, 0x01U };
        while (is_gatt_congested && conn_id);
        if (conn_id)
        {
            wiced_bt_gatt_server_send_notification(conn_id,
                    HDLC_CUSTOM_SERVICE_PROV_STREAM_VALUE,
                    sizeof(done_pkt), done_pkt, NULL);
            LOG_INFO(CYLF_DEF, "[PROV] [0xFF] Transfer complete signal sent\n");
        }
    }
    else
    {
        LOG_ERROR(CYLF_DEF, "[PROV] BLE disconnected before complete signal\n");
    }

    LOG_INFO(CYLF_DEF, "[PROV] Provisioning transfer finished\n");
}

/*******************************************************************************
* Function Name: prov_data_task
********************************************************************************
* Summary:
*  FreeRTOS task that blocks until notified, then sends all provisioning
*  credentials over BLE PROV_STREAM notifications.
*
* Parameters:
*  pvParameters - Unused.
*
* Return:
*  void (never returns)
*
*******************************************************************************/
void prov_data_task(void *pvParameters)
{
    (void)pvParameters;
    for (;;)
    {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        ble_send_provisioning_data();
    }
}

/*******************************************************************************
* Function Name: app_get_attribute
********************************************************************************
* Summary:
*  Looks up a GATT database attribute by its handle.
*
* Parameters:
*  handle - The attribute handle to look up.
*
* Return:
*  gatt_db_lookup_table_t* - Pointer to the attribute entry, or NULL.
*
*******************************************************************************/
static gatt_db_lookup_table_t *app_get_attribute(uint16_t handle)
{
    /* Search for the given handle in the GATT DB and return the pointer to the
    correct attribute */
    uint8_t array_index = 0;

    for (array_index = 0; array_index < app_gatt_db_ext_attr_tbl_size; array_index++)
    {
        if (app_gatt_db_ext_attr_tbl[array_index].handle == handle)
        {
            return (&app_gatt_db_ext_attr_tbl[array_index]);
        }
    }
    return NULL;
}

/*******************************************************************************
* Function Name: app_gatts_req_read_handler
********************************************************************************
* Summary:
*  Handles GATT read requests from the BLE stack.
*
* Parameters:
*  conn_id - Connection ID.
*  opcode - GATT operation code.
*  p_read_data - Pointer to read request data.
*  len_requested - Length of data requested.
*
* Return:
*  wiced_bt_gatt_status_t - GATT operation status.
*
*******************************************************************************/
static wiced_bt_gatt_status_t app_gatts_req_read_handler(uint16_t conn_id,
                                                  wiced_bt_gatt_opcode_t opcode,
                                                  wiced_bt_gatt_read_t *p_read_data,
                                                  uint16_t len_requested)
{

    gatt_db_lookup_table_t *puAttribute;
    int attr_len_to_copy;

    /* Get the right address for the handle in Gatt DB */
    if (NULL == (puAttribute = app_get_attribute(p_read_data->handle)))
    {
        LOG_ERROR(CYLF_DEF, "Read handle attribute not found. Handle:0x%X\n",
                p_read_data->handle);
        wiced_bt_gatt_server_send_error_rsp(conn_id, opcode, p_read_data->handle,
                                            WICED_BT_GATT_INVALID_HANDLE);
        return WICED_BT_GATT_INVALID_HANDLE;
    }

    attr_len_to_copy = puAttribute->cur_len;

    LOG_DEBUG(CYLF_DEF,"GATT Read handler: handle:0x%X, len:%d\n",
           p_read_data->handle, attr_len_to_copy);

    /* If the incoming offset is greater than the current length in the GATT DB
    then the data cannot be read back*/
    if (p_read_data->offset >= puAttribute->cur_len)
    {
        wiced_bt_gatt_server_send_error_rsp(conn_id, opcode, p_read_data->handle,
                                            WICED_BT_GATT_INVALID_OFFSET);
        return (WICED_BT_GATT_INVALID_OFFSET);
    }

    int length_to_send = MIN(len_requested, attr_len_to_copy - p_read_data->offset);

    uint8_t *read_rsp = ((uint8_t *)puAttribute->p_data) + p_read_data->offset;

    wiced_bt_gatt_server_send_read_handle_rsp(conn_id, opcode, length_to_send, read_rsp, NULL);

    return WICED_BT_GATT_SUCCESS;
}

/*******************************************************************************
* Function Name: app_gatt_read_by_type_handler
********************************************************************************
* Summary:
*  Handles GATT read-by-type request events.
*
* Parameters:
*  conn_id - Connection ID.
*  opcode - GATT operation code.
*  p_read_data - Pointer to read-by-type request data.
*  len_requested - Length of data requested.
*
* Return:
*  wiced_bt_gatt_status_t - GATT operation status.
*
*******************************************************************************/
static wiced_bt_gatt_status_t app_gatt_read_by_type_handler(uint16_t conn_id,
                                         wiced_bt_gatt_opcode_t opcode,
                                         wiced_bt_gatt_read_by_type_t *p_read_data,
                                         uint16_t len_requested)
{
    gatt_db_lookup_table_t *puAttribute;
    uint16_t    attr_handle = p_read_data->s_handle;
    uint8_t     *p_rsp = wiced_bt_get_buffer(len_requested);
    uint8_t     pair_len = 0;
    int to_track_len_requested = 0;
    int len_filled;

    if (NULL == p_rsp)
    {
        wiced_bt_gatt_server_send_error_rsp(conn_id, opcode, attr_handle,
                                            WICED_BT_GATT_INSUF_RESOURCE);
        return WICED_BT_GATT_INSUF_RESOURCE;
    }

    /* Read by type returns all attributes of the specified type,
     * between the start and end handles */
    while (WICED_TRUE)
    {
        attr_handle = wiced_bt_gatt_find_handle_by_type(attr_handle,
                p_read_data->e_handle, &p_read_data->uuid);

        if (false == attr_handle)
            break;

        if (NULL ==(puAttribute = app_get_attribute(attr_handle)))
        {
            wiced_bt_gatt_server_send_error_rsp(conn_id, opcode,
                    p_read_data->s_handle, WICED_BT_GATT_ERR_UNLIKELY);
            wiced_bt_free_buffer(p_rsp);
            return WICED_BT_GATT_ERR_UNLIKELY;
        }

        len_filled = wiced_bt_gatt_put_read_by_type_rsp_in_stream(
                p_rsp + to_track_len_requested, len_requested - to_track_len_requested, &pair_len, attr_handle,
                 puAttribute->cur_len, puAttribute->p_data);
        if (INITIAL_LEN_FILLED == len_filled)
        {
            break;
        }
        to_track_len_requested += len_filled;

        /* Increment starting handle for next search to one past current */
        attr_handle++;
    }

    if (INITIAL_LEN_FILLED == to_track_len_requested)
    {
        wiced_bt_gatt_server_send_error_rsp(conn_id, opcode, p_read_data->s_handle,
                                            WICED_BT_GATT_INVALID_HANDLE);
        wiced_bt_free_buffer(p_rsp);
        return WICED_BT_GATT_INVALID_HANDLE;
    }

    /* Send the response */
    wiced_bt_gatt_server_send_read_by_type_rsp(conn_id, opcode, pair_len,
            to_track_len_requested, p_rsp, (wiced_bt_gatt_app_context_t)wiced_bt_free_buffer);

    return WICED_BT_GATT_SUCCESS;
}

/*******************************************************************************
* Function Name: app_gatts_req_write_handler
********************************************************************************
* Summary:
*  Handles GATT write requests from the BLE stack.
*
* Parameters:
*  conn_id - Connection ID.
*  opcode - GATT operation code.
*  p_data - Pointer to write request data.
*
* Return:
*  wiced_bt_gatt_status_t - GATT operation status.
*
*******************************************************************************/
static wiced_bt_gatt_status_t app_gatts_req_write_handler(uint16_t conn_id,
                                                   wiced_bt_gatt_opcode_t opcode,
                                                   wiced_bt_gatt_write_req_t *p_data)
{
    wiced_bt_gatt_status_t result = WICED_BT_GATT_SUCCESS;
    uint8_t *p_attr = p_data->p_val;
    gatt_db_lookup_table_t *puAttribute;

    LOG_DEBUG(CYLF_DEF,"GATT write handler: handle:0x%X len:%d, opcode:0x%X\n",
           p_data->handle, p_data->val_len, opcode);

    /* Get the right address for the handle in Gatt DB */
    if (NULL == (puAttribute = app_get_attribute(p_data->handle)))
    {
        LOG_DEBUG(CYLF_DEF,"Write Handle attr not found. Handle:0x%X\n", p_data->handle);
        return WICED_BT_GATT_INVALID_HANDLE;
    }

    switch (p_data->handle)
    {
        /* Write request for the WiFi SSID characteristic. Copy the incoming data
        to the WIFI SSID variable */
        case HDLC_CUSTOM_SERVICE_WIFI_SSID_VALUE:

            /* First copy the value to the GATT DB variable and then to the global
             * variable to be stored in NV memory
             */
            memset(app_custom_service_wifi_ssid, CRED_INIT_VALUE,
                   strlen((char *)app_custom_service_wifi_ssid));
            memcpy(app_custom_service_wifi_ssid, p_attr, p_data->val_len);
            puAttribute->cur_len = p_data->val_len;

            memcpy(&wifi_cred.wifi_ssid[0], p_attr, p_data->val_len);
            wifi_cred.ssid_len = p_data->val_len;

            LOG_INFO(CYLF_DEF, "Wi-Fi SSID: %s\n", app_custom_service_wifi_ssid);
            break;

        /* Write request for the WiFi password characteristic. Accept the password.
        Copy the incoming data to the WIFI Password variable */
        case HDLC_CUSTOM_SERVICE_WIFI_PASSWORD_VALUE:

            /* First copy the value to the GATT DB variable and then to the global
             * variable to be stored in NV memory
             */
            memset(app_custom_service_wifi_password, CRED_INIT_VALUE,
                    strlen((char *)app_custom_service_wifi_password));
            memcpy(app_custom_service_wifi_password, p_attr, p_data->val_len);
            puAttribute->cur_len = p_data->val_len;

            memcpy(&wifi_cred.wifi_password[0], p_attr, p_data->val_len);
            wifi_cred.password_len = p_data->val_len;

            break;

        /* Handle the CCCD values for WIFI NETWORKS characteristic */
        case HDLD_CUSTOM_SERVICE_WIFI_NETWORKS_CLIENT_CHAR_CONFIG:
            app_custom_service_wifi_networks_client_char_config[0] = p_attr[0];
            app_custom_service_wifi_networks_client_char_config[1] = p_attr[1];
            break;

        /* Write request for the control characteristic. Copy the incoming data
        to the WIFI control variable. Based on the value, start connect, disconnect
         or scan procedure */
        case HDLC_CUSTOM_SERVICE_WIFI_CONTROL_VALUE:
            app_custom_service_wifi_control[0] = p_attr[0];
            puAttribute->cur_len = p_data->val_len;

            /* Connect command */
            if (WIFI_CONTROL_CONNECT == app_custom_service_wifi_control[0])
            {
                if(0 != wifi_cred.ssid_len)
                {
                    is_wifi_cred_need_to_update = true;
                    /* Set the WiFi Connection parameters structure to 0 before copying
                     * data */
                    memset(&wifi_conn_param, CRED_INIT_VALUE, sizeof(cy_wcm_connect_params_t));

                    /* Copy the WiFi credentials to the global variable */
                    memcpy(wifi_conn_param.ap_credentials.SSID,
                           &wifi_cred.wifi_ssid[0], wifi_cred.ssid_len);

                    memcpy(wifi_conn_param.ap_credentials.password,
                           &wifi_cred.wifi_password[0], wifi_cred.password_len);

                    memset(&wifi_details, 0, sizeof(wifi_details_t));
                    memcpy(&wifi_details, &wifi_cred, sizeof(wifi_details_t));

                    update_conn_state(DEV_ST_WIFI_CONNECTING);

                    if((get_mqtt_status() & FLAG_MQTT_CONNECTION_SUCCESS))
                    {
                        mqtt_task_cmd_t mqtt_task_cmd;
                        mqtt_task_cmd = HANDLE_MANUAL_DISCONNECTION;
                        xQueueSend(mqtt_task_q, &mqtt_task_cmd, portMAX_DELAY);
                        vTaskDelay(3000); 
                    }

                    /* Send notification to the WiFi task to scan and connect with
                     * the given WiFi credentials
                     */
                    xTaskNotify(wifi_task_handle, (NOTIF_SCAN | NOTIF_CONNECT),
                                eSetValueWithOverwrite);

                }
                else
                {
                    LOG_INFO(CYLF_DEF, "WiFi Credentials not present\n");
                }
            }
            /* Scan command */
            else if(WIFI_CONTROL_SCAN == app_custom_service_wifi_control[0])
            {
                LOG_INFO(CYLF_DEF, "Rx BLE Wi-Fi scan request.\n");

                /* Check if notifications are enabled or not. If not then there is
                 * no point of starting the scan. If GATT notification is enabled
                 * then ask the WiFi task to start WiFi scan
                 */
                if ((false != conn_id) &&
                    (app_custom_service_wifi_networks_client_char_config[0] &
                    GATT_CLIENT_CONFIG_NOTIFICATION))
                {
                    xTaskNotify(wifi_task_handle, (NOTIF_SCAN), eSetValueWithOverwrite);
                }
                else
                {
                    LOG_INFO(CYLF_DEF, "Notifications for WiFi Networks characteristic are "
                            "disabled. Cannot scan for networks\n");
                }
            }
            /* Disconnect command */
            else if(WIFI_CONTROL_DISCONNECT == app_custom_service_wifi_control[0])
            {
                LOG_INFO(CYLF_DEF, "Notifications for Disconnecting WiFi Networks!\r\n");
                xTaskNotify(wifi_task_handle, NOTIF_DISCONNECT,
                            eSetValueWithOverwrite);
            }
            else
            {
                LOG_INFO(CYLF_DEF, "Invalid command\n");
            }
            break;

        /* Notification for control characteristic.
         */
        case HDLD_CUSTOM_SERVICE_WIFI_CONTROL_CLIENT_CHAR_CONFIG:
            app_custom_service_wifi_control_client_char_config[0] = p_attr[0];
            app_custom_service_wifi_control_client_char_config[1] = p_attr[1];
            break;

        case HDLC_CUSTOM_SERVICE_THERMOSTAT_DATA_VALUE:
        {
            parse_received_command((const char *)p_data->p_val, p_data->val_len);
            /* App is actively talking over BLE -> dismiss the CM55 connectivity/pairing popup. */
            hide_connectivity_popup_ipc();
            break;
        }

        /* CCCD for THERMOSTAT_DATA: required so the app's enable-notification
         * write isn't rejected as an invalid handle. */
        case HDLD_CUSTOM_SERVICE_THERMOSTAT_DATA_CLIENT_CHAR_CONFIG:
            app_custom_service_thermostat_data_client_char_config[0] = p_attr[0];
            app_custom_service_thermostat_data_client_char_config[1] = p_attr[1];
            break;

        /* CCCD for PROV_STREAM: when the app enables notifications,
         * trigger the provisioning credential transfer immediately. */
        case HDLD_CUSTOM_SERVICE_PROV_STREAM_CLIENT_CHAR_CONFIG:
            app_custom_service_prov_stream_client_char_config[0] = p_attr[0];
            app_custom_service_prov_stream_client_char_config[1] = p_attr[1];
            if (p_attr[0] & GATT_CLIENT_CONFIG_NOTIFICATION)
            {
                LOG_INFO(CYLF_DEF, "PROV_STREAM subscribed — starting credential transfer\n");
                if (NULL != prov_data_task_handle)
                {
                    xTaskNotifyGive(prov_data_task_handle);
                }
            }
            break;

        default:
            LOG_ERROR(CYLF_DEF, "Write GATT Handle not found\n");
            result = WICED_BT_GATT_INVALID_HANDLE;
            break;
    }

    return result;
}

/*******************************************************************************
* Function Name: app_gatt_connect_handler
********************************************************************************
* Summary:
*  Handles BLE connection and disconnection events.
*
* Parameters:
*  p_conn_status - Pointer to connection status data.
*
* Return:
*  wiced_bt_gatt_status_t - GATT operation status.
*
*******************************************************************************/
static wiced_bt_gatt_status_t app_gatt_connect_handler(
                       wiced_bt_gatt_connection_status_t *p_conn_status)
{
    wiced_bt_gatt_status_t status = WICED_BT_GATT_ERROR;

    /* Check whether it is a connect event or disconnect event. If the device
    has been disconnected then restart advertisement */
    if (NULL != p_conn_status)
    {
        if (p_conn_status->connected)
        {
            /* Device got connected */
            LOG_INFO(CYLF_DEF, "\nConnected: Peer BD Address: ");
            print_bd_address(p_conn_status->bd_addr);
            LOG_INFO(CYLF_DEF, "\n");
            conn_id = p_conn_status->conn_id;
            memcpy(&peer_addr, &(p_conn_status->bd_addr), sizeof(wiced_bt_device_address_t));

            /* Declare BLE connected up-front only for an already-bonded peer
             * (reconnect, no passkey). For a new pairing, defer until pairing/
             * encryption completes so the UI doesn't flash "connected" before
             * the PIN entry. */
            if (BOND_INDEX_MAX > app_bt_find_device_in_nvm(p_conn_status->bd_addr))
            {
                handle_connectivity_state(STATE_BLE_CONNECTED);
            }
        }
        else /* Device got disconnected */
        {
            LOG_INFO(CYLF_DEF, "\nDisconnected: Peer BD Address: ");
            print_bd_address(p_conn_status->bd_addr);
            LOG_INFO(CYLF_DEF, "\n");

            LOG_INFO(CYLF_DEF, "Reason for disconnection: %s\n",
                    get_bt_gatt_disconn_reason_name(p_conn_status->reason));

            conn_id = false;

            if(GATT_CONN_TERMINATE_LOCAL_HOST != p_conn_status->reason)
            {
                ble_start_advertising(BTM_BLE_ADVERT_UNDIRECTED_HIGH);
            }

            update_comm_interface();

            vTaskDelay(1000);
            update_ui_connectivity_status();
        }

        status = WICED_BT_GATT_SUCCESS;
    }

    return status;
}

/*******************************************************************************
* Function Name: app_gatts_attr_req_handler
********************************************************************************
* Summary:
*  Routes GATT attribute requests to the appropriate handler.
*
* Parameters:
*  p_data - Pointer to the GATT attribute request data.
*
* Return:
*  wiced_bt_gatt_status_t - GATT operation status.
*
*******************************************************************************/
static wiced_bt_gatt_status_t app_gatts_attr_req_handler(wiced_bt_gatt_attribute_request_t *p_data)
{
    wiced_bt_gatt_status_t result = WICED_BT_GATT_INVALID_PDU;
    wiced_bt_gatt_write_req_t *p_write_request = &p_data->data.write_req;

    switch (p_data->opcode)
    {
        case GATT_REQ_READ:
        case GATT_REQ_READ_BLOB:
            result = app_gatts_req_read_handler(p_data->conn_id, p_data->opcode,
                                    &p_data->data.read_req, p_data->len_requested);
            break;

        case GATT_REQ_READ_BY_TYPE:
            result = app_gatt_read_by_type_handler(p_data->conn_id, p_data->opcode,
                                &p_data->data.read_by_type, p_data->len_requested);
            break;

        case GATT_REQ_WRITE:
        case GATT_CMD_WRITE:
        case GATT_CMD_SIGNED_WRITE:
            result = app_gatts_req_write_handler(p_data->conn_id, p_data->opcode,
                                                 &(p_data->data.write_req));
            if ((p_data->opcode == GATT_REQ_WRITE) && (result == WICED_BT_GATT_SUCCESS))
            {
                wiced_bt_gatt_server_send_write_rsp(p_data->conn_id, p_data->opcode,
                                                    p_write_request->handle);
            }
            else
            {
                wiced_bt_gatt_server_send_error_rsp(p_data->conn_id, p_data->opcode,
                                                    p_write_request->handle, result);
            }
            break;

        case GATT_REQ_MTU:
            LOG_INFO(CYLF_DEF, "Exchanged MTU from client: %d\n", p_data->data.remote_mtu);
            wiced_bt_gatt_server_send_mtu_rsp(p_data->conn_id, p_data->data.remote_mtu,
                    wiced_bt_cfg_settings.p_ble_cfg->ble_max_rx_pdu_size);
            result = WICED_BT_GATT_SUCCESS;
            break;

        default:
            break;
    }

    return result;
}

/*******************************************************************************
* Function Name: app_gatts_callback
********************************************************************************
* Summary:
*  Main GATT event callback dispatch function.
*
* Parameters:
*  event - The GATT event type.
*  p_data - Pointer to event data.
*
* Return:
*  wiced_bt_gatt_status_t - GATT operation status.
*
*******************************************************************************/
wiced_bt_gatt_status_t app_gatts_callback(wiced_bt_gatt_evt_t event,
                                          wiced_bt_gatt_event_data_t *p_data)
{
    wiced_bt_gatt_status_t result = WICED_BT_GATT_INVALID_PDU;

    LOG_DEBUG(CYLF_DEF,"GATTS event: %s\n", get_bt_gatt_evt_name(event));

    switch (event)
    {
        case GATT_CONNECTION_STATUS_EVT:
            result = app_gatt_connect_handler(&p_data->connection_status);
            break;

        case GATT_ATTRIBUTE_REQUEST_EVT:
            result = app_gatts_attr_req_handler(&p_data->attribute_request);
            break;


        case GATT_CONGESTION_EVT:
            LOG_INFO(CYLF_DEF, "\nGATT_CONGESTION_EVT\n");

            num_of_congestions++;
            is_gatt_congested = (p_data->congestion.congested) ? true : false;

            if (!is_gatt_congested)
            {
                /* Do nothing */
            }
            break;


        case GATT_GET_RESPONSE_BUFFER_EVT:
            p_data->buffer_request.buffer.p_app_rsp_buffer = wiced_bt_get_buffer(
                                              p_data->buffer_request.len_requested);

            p_data->buffer_request.buffer.p_app_ctxt = (void *)wiced_bt_free_buffer;

            if(NULL == p_data->buffer_request.buffer.p_app_rsp_buffer)
            {
                LOG_INFO(CYLF_DEF, "Insufficient resources\n");
                result = WICED_BT_GATT_INSUF_RESOURCE;
            }
            else
            {
                result = WICED_BT_GATT_SUCCESS;
            }
            break;

        case GATT_APP_BUFFER_TRANSMITTED_EVT:
        {
            pfn_free_buffer_t pfn_free = (pfn_free_buffer_t)p_data->
                                          buffer_xmitted.p_app_ctxt;

            /* If the buffer is dynamic, the context will point to a function to
             * free it.
             */
            if (pfn_free)
                pfn_free(p_data->buffer_xmitted.p_app_data);

            result = WICED_BT_GATT_SUCCESS;
        }
        break;

        default:
            LOG_INFO(CYLF_DEF, "GATT event not handled\n");
            break;
    }
    return result;
}

/*******************************************************************************
* Function Name: app_bt_send_message
********************************************************************************
* Summary:
*  Sends GATT notification data over BLE.
*
* Parameters:
*  data - Pointer to message data.
*  data_len - Length of the message data.
*
* Return:
*  void
*
*******************************************************************************/
void app_bt_send_message(char *data, size_t data_len)
{
    if(conn_id)
    {
        /* Wait if GATT congestion flag enabled. */
        while (is_gatt_congested);

        wiced_bt_gatt_server_send_notification(conn_id,
                HDLC_CUSTOM_SERVICE_THERMOSTAT_DATA_VALUE,data_len, (uint8_t *)data, NULL);
        LOG_INFO(CYLF_DEF, "Send JSON Package Over BLE:\n%s\n", data);

        free(data);

        /* Wait for GATT congestion to clear */
        while (is_gatt_congested);
    }
    else
    {
        LOG_INFO(CYLF_DEF, "BLE Not Connected!\n");
    }
}

/*******************************************************************************
* Function Name: get_ble_connection_status
********************************************************************************
* Summary:
*  Returns the current BLE connection status.
*
* Parameters:
*  void
*
* Return:
*  bool - true if BLE is connected.
*
*******************************************************************************/
bool get_ble_connection_status(void)
{
    return (bool)conn_id;
}

/* [] END OF FILE */
