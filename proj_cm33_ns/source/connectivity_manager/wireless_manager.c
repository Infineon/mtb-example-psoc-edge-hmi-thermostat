/*******************************************************************************
 * File Name:   wireless_manager.c
 *
 * Description: Implementation of Wireless Manager handling BLE and Wi-Fi
 *              communication.
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
#include "mqtt_client_config.h"


/*******************************************************************************
 *                                MACROS
 *****************************************************************************/
/* LE Key Size */
#define MAX_KEY_SIZE (16U)

/* WICED Heap size for app */
#define APP_HEAP_SIZE              (0x1000U)
#define DEBOUNCE_DELAY_MS          (300U)
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
wifi_details_t wifi_cred;

/* Task Handle for WiFi Task */
TaskHandle_t wifi_task_handle;

/* Maintains the connection id of the current connection */
uint16_t conn_id = 0;
static wiced_bt_device_address_t peer_addr;

/* Pointer to the heap created */
wiced_bt_heap_t * app_heap_pointer;

uint8_t wifi_mac[6] = {0};
uint8_t ble_name[MAX_LEN_GAP_DEVICE_NAME+1] = {0};

bool cloud_connection_inprogress = false;
bool provision_inprogress = false;
static bool ble_stack_init_state = false;
bool device_provisioned = false;
static uint8_t ble_retry_attempt = 0;
static bool is_manual_low_adv = false;
static connectivity_state_t current_state         = STATE_UNPROVISIONED;
connection_retries_t wifi_auto_reconnect_retry     = WIFI_CONN_RETRY_LIMITED;
connection_retries_t cloud_auto_reconnect_retry = CLOUD_CONN_RETRY_LIMITED;
connection_retries_t ble_auto_reconnect_retry     = BLE_CONN_RETRY_LIMITED;

/* This variable tracks the number of congestion events during BLE data transfer */
uint16_t num_of_congestions = 0;

/* Flag to check for GATT congestion */
uint8_t is_gatt_congested;

/* Task handle for the BLE provisioning data sender task */
static TaskHandle_t prov_data_task_handle = NULL;

/*****************************************************************************
 * Static Function Prototype
 *****************************************************************************/
static void ble_send_provisioning_field(uint8_t type, const char *data, size_t total_len);
static void ble_send_provisioning_data(void);
static void prov_data_task(void *pvParameters);

/*******************************************************************************
* Function Name: app_get_attribute
********************************************************************************
* Summary:
* This function searches through the GATT DB to point to the attribute
* corresponding to the given handle
*
* Parameters:
*  uint16_t handle: Handle to search for in the GATT DB
*
* Return:
*  gatt_db_lookup_table_t *: Pointer to the correct attribute in the GATT DB
*
*******************************************************************************/
static gatt_db_lookup_table_t *app_get_attribute(uint16_t handle);

/*******************************************************************************
* Function Name: app_gatts_req_read_handler
********************************************************************************
* Summary:
* This function handles the GATT read request events from the stack
*
* Parameters:
*  uint16_t conn_id: Connection ID
*  wiced_bt_gatt_read_t * p_read_data: Read data structure
*  wiced_bt_gatt_opcode_t opcode: GATT opcode
*  uint16_t len_requested: Length requested
* Return:
*  wiced_bt_gatt_status_t: GATT result
*
*******************************************************************************/
static wiced_bt_gatt_status_t app_gatts_req_read_handler(uint16_t conn_id,
                                                  wiced_bt_gatt_opcode_t opcode,
                                                  wiced_bt_gatt_read_t *p_read_data,
                                                  uint16_t len_requested);

/*******************************************************************************
* Function Name: app_gatt_read_by_type_handler
********************************************************************************
* Summary:
* This function handles the GATT read by type request events from the stack
*
* Parameters:
*  uint16_t conn_id: Connection ID
*  wiced_bt_gatt_opcode_t opcode: GATT opcode
*  wiced_bt_gatt_read_by_type_t * p_read_data: Read data structure
*  uint16_t len_requested: Length requested
*
* Return:
*  wiced_bt_gatt_status_t: GATT result
*
*******************************************************************************/
static wiced_bt_gatt_status_t app_gatt_read_by_type_handler(uint16_t conn_id,
                                         wiced_bt_gatt_opcode_t opcode,
                                         wiced_bt_gatt_read_by_type_t *p_read_data,
                                         uint16_t len_requested);

/*******************************************************************************
* Function Name: app_gatts_req_write_handler
********************************************************************************
* Summary:
* This function handles the GATT write request events from the stack
*
* Parameters:
*  uint16_t conn_id: Connection ID
*  wiced_bt_gatt_opcode_t opcode: GATT opcode
*  wiced_bt_gatt_write_t * p_data: Write data structure
*
* Return:
*  wiced_bt_gatt_status_t: GATT result
*
*******************************************************************************/
static wiced_bt_gatt_status_t app_gatts_req_write_handler(uint16_t conn_id,
                                                   wiced_bt_gatt_opcode_t opcode,
                                                   wiced_bt_gatt_write_req_t *p_data);
/*******************************************************************************
* Function Name: app_gatt_connect_handler
********************************************************************************
* Summary:
* This function handles the GATT connect request events from the stack
*
* Parameters:
*  wiced_bt_gatt_connection_status_t *p_conn_status: Connection or disconnection
*
* Return:
*  wiced_bt_gatt_status_t: GATT result
*
*******************************************************************************/
static wiced_bt_gatt_status_t app_gatt_connect_handler(
                       wiced_bt_gatt_connection_status_t *p_conn_status);

/*******************************************************************************
* Function Name: app_gatts_attr_req_handler
********************************************************************************
* Summary:
* This function redirects the GATT attribute requests to the appropriate
* functions
*
* Parameters:
*  wiced_bt_gatt_attribute_request_t *p_data: GATT request data structure
*
* Return:
*  wiced_bt_gatt_status_t: GATT result
*
*******************************************************************************/
static wiced_bt_gatt_status_t app_gatts_attr_req_handler(wiced_bt_gatt_attribute_request_t *p_data);

/*******************************************************************************
* Function Name: app_gatts_callback
********************************************************************************
* Summary:
* This function redirects the GATT requests to the appropriate functions
*
* Parameters:
*  wiced_bt_gatt_attribute_request_t *p_data: GATT request data structure
*  wiced_bt_gatt_event_data_t *p_data       : Pointer to BLE GATT event structures
* Return:
*  wiced_bt_gatt_status_t: GATT result
*
*******************************************************************************/
static wiced_bt_gatt_status_t app_gatts_callback(wiced_bt_gatt_evt_t event,
                                          wiced_bt_gatt_event_data_t *p_data);

static wiced_result_t app_management_callback(wiced_bt_management_evt_t event,
                                       wiced_bt_management_evt_data_t *p_event_data);

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
static void application_init(void);

/*****************************************************************************
 * Static Function Definitions
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
static void prov_data_task(void *pvParameters)
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
            break;
        }

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
    wiced_result_t result;

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
            handle_connectivity_state(STATE_BLE_CONNECTED);
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
static wiced_bt_gatt_status_t app_gatts_callback(wiced_bt_gatt_evt_t event,
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
    wiced_bt_device_address_t bda = {0};
    wiced_bt_dev_ble_io_caps_req_t *pairing_io_caps = &(p_event_data->
                                           pairing_io_capabilities_ble_request);

    LOG_DEBUG(CYLF_DEF, "Bluetooth Management Event: %s\n", get_btm_event_name(event));

    switch (event)
    {
        case BTM_ENABLED_EVT:

            /* Bluetooth is enabled */
            wiced_bt_set_local_bdaddr((uint8_t *)cy_bt_device_address,
                                       BLE_ADDR_PUBLIC);

            /* Read and print the BD address */
            wiced_bt_dev_read_local_addr(bda);
            LOG_INFO(CYLF_DEF, "Local Bluetooth Address: ");
            print_bd_address(bda);
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
            pairing_io_caps->local_io_cap = BTM_IO_CAPABILITIES_NONE;

            pairing_io_caps->oob_data = BTM_OOB_NONE;

            pairing_io_caps->auth_req = BTM_LE_AUTH_REQ_SC;

            pairing_io_caps->max_key_size = MAX_KEY_SIZE;

            pairing_io_caps->init_keys = BTM_LE_KEY_PENC | BTM_LE_KEY_PID |
                                        BTM_LE_KEY_PCSRK | BTM_LE_KEY_LENC;

            pairing_io_caps->resp_keys = BTM_LE_KEY_PENC | BTM_LE_KEY_PID |
                                        BTM_LE_KEY_PCSRK | BTM_LE_KEY_LENC;

            break;

        case BTM_PAIRING_COMPLETE_EVT:
            if (WICED_SUCCESS == p_event_data->
                                pairing_complete.pairing_complete_info.ble.status)
            {
                LOG_INFO(CYLF_DEF, "Pairing Complete: SUCCESS\n");
                handle_connectivity_state(STATE_BLE_CONNECTED);
            }
            else /* Pairing Failed */
            {
                LOG_INFO(CYLF_DEF, "Pairing Complete: FAILED\n");
            }
            break;

        case BTM_PAIRED_DEVICE_LINK_KEYS_UPDATE_EVT:
            /* Paired Device Link Keys update */
            result = WICED_SUCCESS;
            break;

        case BTM_PAIRED_DEVICE_LINK_KEYS_REQUEST_EVT:
            /* Paired Device Link Keys Request */
            result = WICED_BT_ERROR;
            break;

        case BTM_LOCAL_IDENTITY_KEYS_UPDATE_EVT:
            /* Local identity Keys Update */
            result = WICED_SUCCESS;
            break;

        case BTM_LOCAL_IDENTITY_KEYS_REQUEST_EVT:
            /* Local identity Keys Request */
            result = WICED_BT_ERROR;
            break;

        case BTM_ENCRYPTION_STATUS_EVT:
            if (WICED_SUCCESS == p_event_data->encryption_status.result)
            {
                LOG_DEBUG(CYLF_DEF, "Encryption Status Event: SUCCESS\n");
            }
            else /* Encryption Failed */
            {
                LOG_DEBUG(CYLF_DEF, "Encryption Status Event: FAILED\n");
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
*  Initializes GATT DB, pairing, and BLE advertisement.
*
* Parameters:
*  void
*
* Return:
*  void
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
* Function Name: wifi_init
********************************************************************************
* Summary:
*  Initializes the Wi-Fi connection manager module.
*
* Parameters:
*  void
*
* Return:
*  cy_rslt_t - CY_RSLT_SUCCESS on success.
*
*******************************************************************************/
cy_rslt_t wifi_init(void)
{
    return wcm_init();
}

/*******************************************************************************
* Function Name: wifi_disconnect
********************************************************************************
* Summary:
*  Disconnects from the current Wi-Fi access point.
*
* Parameters:
*  void
*
* Return:
*  cy_rslt_t - CY_RSLT_SUCCESS on success.
*
*******************************************************************************/
cy_rslt_t wifi_disconnect(void)
{
    return cy_wcm_disconnect_ap();
}

/*******************************************************************************
* Function Name: wifi_get_status
********************************************************************************
* Summary:
*  Returns Wi-Fi AP connection status.
*
* Parameters:
*  void
*
* Return:
*  bool - true if connected to AP.
*
*******************************************************************************/
bool wifi_get_status(void)
{
    return cy_wcm_is_connected_to_ap();
}

/*******************************************************************************
* Function Name: wifi_get_macaddr
********************************************************************************
* Summary:
*  Gets the Wi-Fi station MAC address.
*
* Parameters:
*  mac - Pointer to buffer for MAC address.
*
* Return:
*  void
*
*******************************************************************************/
void wifi_get_macaddr(uint8_t *mac)
{
    cy_wcm_get_mac_addr(CY_WCM_INTERFACE_TYPE_STA, (cy_wcm_mac_t *)mac);
}

/*******************************************************************************
* Function Name: delete_wifi_credential
********************************************************************************
* Summary:
*  Deletes stored Wi-Fi credentials from NVM.
*
* Parameters:
*  void
*
* Return:
*  void
*
*******************************************************************************/
void delete_wifi_credential(void)
{
    LOG_ERROR(CYLF_DEF, "Deleting Wi-Fi data from NVM\n");

    /* Set the data to 0*/
    memset(&wifi_cred, 0, sizeof(wifi_cred));
    memset(&wifi_conn_param, 0, sizeof(wifi_conn_param));

    Cy_RRAM_WriteByteArray(RRAMC0, APP_RRAM_NVM_MAIN_NS_START + RRAM_NVM_WIFI_CRED_OFFSET , &wifi_cred.wifi_ssid[0],sizeof(wifi_cred));
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
            LOG_ERROR(CYLF_DEF, "ADV does not need to be started again because it is already active\r\n");
        }
        return WICED_SUCCESS;
    }

    if(!conn_id)
    {
        ble_stop_advertising();
        vTaskDelay(500);

        if(WICED_SUCCESS != wiced_bt_ble_set_raw_advertisement_data(CY_BT_ADV_PACKET_DATA_SIZE,
                                                                                   cy_bt_adv_packet_data))
        {
            LOG_ERROR(CYLF_DEF, "Set ADV data failed\n");
        }

        update_conn_state(DEV_ST_BLE_ADVERTISING);
        return wiced_bt_start_advertisements(mode, false, NULL);
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
