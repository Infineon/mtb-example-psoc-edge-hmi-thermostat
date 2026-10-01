/*******************************************************************************
 * File Name:  app_ui_receiver.c
 *
 * Description: UI receiver module implementation.
 *
 * This file implements the UI receiver task, which handles events and messages
 * from various sources, including the inter-processor communication (IPC) pipe.
 *
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
 *                              INCLUDES
 ******************************************************************************/
#include "app_ui_receiver.h"
#include "connectivity_manager/mqtt/mqtt_command_handler.h"
#include "connectivity_manager/wireless_manager.h"
#include "app_bt_event_handler.h"
#include "app_bt_gatt_handler.h"
#include "wifi_task.h"
#include "mqtt_task.h"
#include "publisher_task.h"
#include "connectivity_manager/http/secure_http_client.h"

/*******************************************************************************
 *                              CONSTANTS
 ******************************************************************************/
#define UI_RX_TASK_NAME                       ("UIRxTask")

/* stack size in words */
#define UI_RX_TASK_STACK_SIZE                   (1 * 1024)

#define UI_RX_TASK_PRIORITY                     (configMAX_PRIORITIES - 1)
#define UI_RX_QUEUE_LENGTH                      20U
/*******************************************************************************
 *                              GLOBAL VARIABLES
 ******************************************************************************/
/* Task handler for communication with the UI_RX task */
TaskHandle_t cm33_ui_rx_task_handle = NULL;

bool update_credential = false;
QueueHandle_t xUiRxQueue = NULL;

static bool is_forced_wifi_connection = false;

/*******************************************************************************
 *                              STATIC VARIABLES
 ******************************************************************************/
static volatile uint32_t ui_rx_msg_data = 0;
static volatile uint32_t ui_rx_msg_cmd = 0;

/****************************************************************************
 *                              FUNCTION DECLARATIONS
 ***************************************************************************/
static void ui_rx_task(void *arg);
/*******************************************************************************
 *                              FUNCTION DEFINITIONS
 ******************************************************************************/
/*******************************************************************************
* Function Name: get_latest_fw_version
********************************************************************************
* Summary:
*  Requests the latest firmware version from the cloud and publishes the
*  request through the MQTT path when the device is connected.
*
* Parameters:
*  void
*
* Return:
*  void
*
*******************************************************************************/
void get_latest_fw_version(void)
{
    char payload[50];

    if((cy_wcm_is_connected_to_ap() == 0) || (!(get_mqtt_status() & FLAG_MQTT_CONNECTION_SUCCESS)))
    {
        no_internet_connected_send_to_ui();
        return;
    }

    snprintf(payload, sizeof(payload), "{\"Message\":\"Update Version\",\"UID\":\"%X%X%X\"}", wifi_mac[3], wifi_mac[4], wifi_mac[5]);
    publish_msg_for_FW_version(payload);
}

/*******************************************************************************
* Function Name: ui_rx_task
********************************************************************************
* Summary:
*  Task that receives and processes IPC messages from the CM55 UI core.
*
* Parameters:
*  arg - Task argument (unused).
*
* Return:
*  void
*
*******************************************************************************/
static void ui_rx_task(void *arg)
{
    (void) arg;
    mqtt_task_cmd_t mqtt_task_cmd;
    ipc_msg_t received_msg = {0};

    while (1)
    {
        /* Wait for events via the message queue (block indefinitely) */
        if (xQueueReceive(xUiRxQueue, &received_msg, portMAX_DELAY) == pdPASS)
        {
            /* Message successfully received. */
            LOG_INFO(CYLF_DEF, "Received message CMD: %lu\n", received_msg.cmd);

            /* Parse the received msg command and value */
            ui_rx_msg_data = received_msg.data;
            ui_rx_msg_cmd = received_msg.cmd;

            switch (ui_rx_msg_cmd)
            {
                case IPC_CMD_GET_UID:
                {
                    LOG_INFO(CYLF_DEF, "Request to send UID received\n");
                    LOG_INFO(CYLF_DEF, "BLE Dev name: %s\n", (char*) ble_name);
                    response_uid_req((char*) ble_name);
                    break;
                }
            
                case IPC_CMD_SET_DISPLAY_BRIGHTNESS:
                {
                    LOG_INFO(CYLF_DEF, "Rx Brightness: %u\n", ui_rx_msg_data);
                    send_response_numeric(DEVICE_BRIGHTNESS, OPERATION_READ, (uint32_t) ui_rx_msg_data);
                    break;
                }
            
                case IPC_CMD_RESET_WIFI_SSID_PASS:
                {
                    LOG_INFO(CYLF_DEF, "Rx Wi-Fi credential erase request.\n");
                    is_wifi_connection_cancel = true;
                    set_wifi_switch_disabled();
                    handle_pairingremove_command(true);

                    /* Wake the HTTPS task from its 15-minute sleep so it
                    * fetches immediately once Wi-Fi reconnects after reset. */
                    if (https_task_handle != NULL)
                    {
                        xTaskNotifyGive(https_task_handle);
                    }
                    break;
                }
                
                case IPC_CMD_SET_FAN_SPEED:
                {
                    device_status.thermostat_settings.fan_speed = ui_rx_msg_data;
                    send_response_numeric(FAN_SPEED, OPERATION_READ,
                                          (uint32_t) device_status.thermostat_settings.fan_speed);
                    LOG_INFO(CYLF_DEF, "Rx Fan Mode: %u\n", ui_rx_msg_data);
                    break;
                }
            
                case IPC_CMD_SET_THERMOSTAT_MODE:
                {
                    device_status.thermostat_settings.mode = ui_rx_msg_data;
                    send_response_numeric(DEVICE_MODE, OPERATION_READ,
                                          (uint32_t) device_status.thermostat_settings.mode);
                    LOG_INFO(CYLF_DEF, "Rx Thermostat Mode: %u\n", ui_rx_msg_data);
                    break;
                }
            
                case IPC_CMD_SET_CURRENT_TEMP:
                {
                    device_status.environment.current_temp = ui_rx_msg_data;
                    send_response_numeric(CURRENT_TEMP, OPERATION_READ,
                                          (uint32_t) device_status.environment.current_temp);
                    LOG_INFO(CYLF_DEF, "Rx Current temp: %u\n", ui_rx_msg_data);
                    break;
                }
            
                case IPC_CMD_SET_TARGET_TEMP:
                {
                    device_status.environment.target_temp = ui_rx_msg_data;
                    send_response_numeric(TARGETED_TEMP, OPERATION_READ,
                                          (uint32_t) device_status.environment.target_temp);
                    LOG_INFO(CYLF_DEF, "Rx Target temp: %u\n", ui_rx_msg_data);
                    break;
                }
            
                case IPC_CMD_SET_TEMPERATURE_DATA:
                {
                    LOG_INFO(CYLF_DEF, "Rx IPC_CMD_SET_TEMPERATURE_DATA.\n");
            
                    static device_state_t temp_data = { 0 };
                    static device_state_t prev_data = { 0 };
                    static int32_t last_sent_time_minutes = -1;
            
                    temp_data.environment.current_temp = received_msg.device_config.environment.current_temp;
                    temp_data.environment.target_temp = received_msg.device_config.environment.target_temp;
                    temp_data.thermostat_settings.time_remains =
                        received_msg.device_config.thermostat_settings.time_remains;
            
                    LOG_INFO(CYLF_DEF, "Current T: %u, Target T: %u, RS: %u\n",
                             temp_data.environment.current_temp,
                             temp_data.environment.target_temp,
                             temp_data.thermostat_settings.time_remains);
            
                    if (temp_data.environment.target_temp != prev_data.environment.target_temp)
                    {
                        prev_data.environment.target_temp = temp_data.environment.target_temp;
                        device_status.environment.target_temp = temp_data.environment.target_temp;
            
                        /* Send the target temperature to the mobile app. */
                        for (int i = 0; i < 1; i++)
                        {
                            send_response_numeric(TARGETED_TEMP, OPERATION_READ,
                                                  (uint32_t) device_status.environment.target_temp);
                            vTaskDelay(pdMS_TO_TICKS(5));
                        }
                    }
            
                    if (temp_data.environment.current_temp != prev_data.environment.current_temp)
                    {
                        prev_data.environment.current_temp = temp_data.environment.current_temp;
                        device_status.environment.current_temp = temp_data.environment.current_temp;
                        send_response_numeric(CURRENT_TEMP, OPERATION_READ,
                                              (uint32_t) device_status.environment.current_temp);
                    }
            
                    /* Check if the seconds value has changed. */
                    if (temp_data.thermostat_settings.time_remains != prev_data.thermostat_settings.time_remains)
                    {
                        /* Update the local status first. */
                        prev_data.thermostat_settings.time_remains = temp_data.thermostat_settings.time_remains;
                        device_status.thermostat_settings.time_remains = temp_data.thermostat_settings.time_remains;
            
                        /* Calculate the current number of minutes for comparison. */
                        int32_t current_minutes = device_status.thermostat_settings.time_remains / 60;
            
                        /* Only send to the cloud if the minute value has changed. */
                        if (current_minutes != last_sent_time_minutes)
                        {
                            last_sent_time_minutes = current_minutes;
            
                            /* Send the actual value in seconds, as required by the cloud. */
                            send_response_numeric(TIME_REMAINING, OPERATION_READ,
                                                  (uint32_t) device_status.thermostat_settings.time_remains);
                        }
                    }
                    break;
                }
            
                case IPC_CMD_SWITCH_TO_BLE:
                {
                    LOG_INFO(CYLF_DEF, "Rx IPC_CMD_SWITCH_TO_BLE.\n");
            
                    /* Check if device is connected over BLE */
                    if (conn_id != true)
                    {
                        /* Send the connection status to end device */
                        for (int retry = 0; retry < 5; retry++)
                        {
                            send_response_numeric(DEVICE_SWITCH_TO_BLE, OPERATION_READ, 1);
                            vTaskDelay(pdMS_TO_TICKS(200));
                        }
                    }
                    else
                    {
                        LOG_INFO(CYLF_DEF, "Device already connected over BLE\n");
                    }

                    set_wifi_switch_disabled();
                    ble_start_advertising(BTM_BLE_ADVERT_UNDIRECTED_HIGH);
                    break;
                }
            
                case IPC_CMD_SET_REMAINING_TIME:
                {
                    device_status.thermostat_settings.time_remains = ui_rx_msg_data;
                    send_response_numeric(TIME_REMAINING, OPERATION_READ,
                                          (uint32_t) device_status.thermostat_settings.time_remains);
                    LOG_INFO(CYLF_DEF, "Rx Remaining time: %u\n", ui_rx_msg_data);
                    break;
                }
            
                case IPC_CMD_UPDATE_CONN_RETRIES:
                {
                    connection_retries_t received_auto = (connection_retries_t)ui_rx_msg_data;
                    if ((WIFI_CONN_RETRY_INFINITE == received_auto) && get_device_provision_state())
                    {
                        set_retry_state(WIFI_INTERFACE, received_auto);
                        update_conn_state(DEV_ST_WIFI_CONNECTING);
                        xTaskNotify(wifi_task_handle, (NOTIF_SCAN | NOTIF_CONNECT), eSetValueWithOverwrite);
                    }
                    else if (WIFI_CONN_RETRY_LIMITED == received_auto)
                    {
                        set_retry_state(WIFI_INTERFACE, received_auto);
                    }
                    else if ((CLOUD_CONN_RETRY_INFINITE == (connection_retries_t) ui_rx_msg_data))
                    {
                        set_retry_state(CLOUD_INTERFACE, received_auto);
                        mqtt_task_cmd_t mqtt_task_cmd = HANDLE_CONNECT;
                        xQueueSend(mqtt_task_q, &mqtt_task_cmd, portMAX_DELAY);
                    }
                    else if (CLOUD_CONN_RETRY_LIMITED == received_auto)
                    {
                        set_retry_state(CLOUD_INTERFACE, received_auto);
                    }
                    LOG_INFO(CYLF_DEF, "Rx Auto Retries: %u\r\n", received_auto);
                    break;
                }
            
                case IPC_CMD_UPDATE_CONN_STATE:
                {
                    switch ((device_connection_state_t) ui_rx_msg_data)
                    {
                        case DEV_ST_CLOUD_CONNECTION_CANCEL:
                        {
                            LOG_INFO(CYLF_DEF, "Rx DEV_ST_CLOUD_CONNECTION_CANCEL.\n");
                            is_cloud_connection_cancel = true;
                            set_wifi_switch_disabled();
                            break;
                        }
            
                        case DEV_ST_WIFI_CONNECTION_CANCEL:
                        {
                            LOG_INFO(CYLF_DEF, "Rx DEV_ST_WIFI_CONNECTION_CANCEL.\n");
                            set_wifi_switch_disabled();
                            is_wifi_connection_cancel = true;
            
                            /* Check if BLE is connected or not, and initiate actions accordingly. */
                            if (conn_id != false)
                            {
                                LOG_INFO(CYLF_DEF, "DEV_ST_WIFI_CONNECTION_CANCEL: BLE already connected.\n");
                                update_conn_state(DEV_ST_BLE_CONNECTED);
                                handle_connectivity_state(STATE_BLE_CONNECTED);
                            }
                            else
                            {
                                /* No BLE adv state change here - it caused a wifi/ble UI ping-pong on cancel */
                                update_comm_interface();
                            }

                            xTaskNotify(wifi_task_handle, NOTIF_DISCONNECT, eSetValueWithOverwrite);
                            break;
                        }
            
                        case DEV_ST_BLE_ADVERTISEMENT_CANCEL:
                        {
                            LOG_INFO(CYLF_DEF, "Rx DEV_ST_BLE_ADVERTISEMENT_CANCEL.\n");
                            ble_stop_advertising();
                            if (get_device_provision_state())
                            {
                                vTaskDelay(pdMS_TO_TICKS(100));
                                update_conn_state(DEV_ST_WIFI_CONNECTING);
                                xTaskNotify(wifi_task_handle, (NOTIF_SCAN | NOTIF_CONNECT), eSetValueWithOverwrite);
                            }
                            else
                            {
                                update_conn_state(DEV_ST_UNPROVISIONED);
                                LOG_INFO(CYLF_DEF, "Wi-Fi credential not available.\n");
                            }
                            update_comm_interface();
                            break;
                        }
            
                        case DEV_ST_BLE_ADVERTISING:
                        {
                            LOG_INFO(CYLF_DEF, "Rx DEV_ST_BLE_ADVERTISING. Starting BLE advertising.\n");
                            handle_connectivity_state(STATE_START_PROVISIONING);
                            break;
                        }
            
                        case DEV_ST_WIFI_CONNECTING:
                        {
                            LOG_INFO(CYLF_DEF, "Rx DEV_ST_WIFI_CONNECTING.\n");
                            wifi_credentials_t info = { 0 };
            
                            /* Safe copy with null termination */
                            strncpy(info.ssid, received_msg.wifi_info.ssid, sizeof(info.ssid) - 1);
                            info.ssid[sizeof(info.ssid) - 1] = '\0';
            
                            strncpy(info.password, received_msg.wifi_info.password, sizeof(info.password) - 1);
                            info.password[sizeof(info.password) - 1] = '\0';
            
                            LOG_INFO(CYLF_DEF, "Received SSID: %s", info.ssid);
            
                            /* Set the Wi-Fi connection parameters structure to 0 before copying data */
                            memset(&wifi_conn_param, 0, sizeof(cy_wcm_connect_params_t));
            
                            /* Copy the Wi-Fi credentials to the global variable */
                            memcpy(wifi_conn_param.ap_credentials.SSID, &info.ssid[0], sizeof(info.ssid));
                            memcpy(wifi_conn_param.ap_credentials.password, &info.password[0], sizeof(wifi_conn_param.ap_credentials.password));
                            
                            is_wifi_cred_need_to_update = true;
                            
                            if (device_provisioned)
                            {
                                mqtt_task_cmd = HANDLE_MANUAL_DISCONNECTION;
                                xQueueSend(mqtt_task_q, &mqtt_task_cmd, portMAX_DELAY);
                                update_credential = true;
                            }
                            else
                            {
                                memset(&wifi_details, 0, sizeof(wifi_details_t));
                        memcpy(wifi_details.wifi_ssid, &info.ssid[0], sizeof(info.ssid));
                    memcpy(wifi_details.wifi_password, &info.password[0], sizeof(wifi_details.wifi_password));
                        wifi_details.ssid_len = strlen((const char *)wifi_details.wifi_ssid);
                        wifi_details.password_len = strlen((const char *)wifi_details.wifi_password);

                                /* Unblock Wi-Fi task with notification */
                                xTaskNotify(wifi_task_handle, (NOTIF_SCAN | NOTIF_CONNECT), eSetValueWithOverwrite);
                            }
                            break;
                        }

                        case DEV_ST_SWITCH_WIFI:
                        {
                            LOG_INFO(CYLF_DEF, "Rx DEV_ST_SWITCH_WIFI.\n");                
                            is_forced_wifi_connection = true;

                            /* Send the connection status to end device */
                            for (int retry = 0; retry < 1; retry++)
                            {
                                send_response_numeric(DEVICE_SWITCH_TO_WIFI, OPERATION_READ, 1);
                                vTaskDelay(pdMS_TO_TICKS(200));
                            }
                    
                            ble_disconnect();
                            
                            if (get_device_provision_state())
                            {
                                xTaskNotify(wifi_task_handle, (NOTIF_SCAN | NOTIF_CONNECT), eSetValueWithOverwrite);
                            }
                            else
                            {
                                update_conn_state(DEV_ST_UNPROVISIONED);
                                LOG_INFO(CYLF_DEF, "Wi-Fi credential not available.\n");
                            }
                    
                            LOG_INFO(CYLF_DEF, "Request to switch to Wi-Fi connection\n");
                            break;
                        }
            
                        default:
                        {
                            break;
                        }
                    }
                    break;
                }
            
                case IPC_CMD_DEVICE_CONFIG:
                {
                    LOG_INFO(CYLF_DEF, "Rx IPC_CMD_DEVICE_CONFIG.\n");
                    device_status = received_msg.device_config;
            
                    /* Send received device info over BLE/MQTT */
                    send_device_config_info();
                    break;
                }
            
                case IPC_CMD_GET_CURRENT_HUMIDITY:
                {
                    LOG_INFO(CYLF_DEF, "Rx IPC_CMD_GET_CURRENT_HUMIDITY: %u%%\n", received_msg.data);
                    device_status.environment.current_humidity = received_msg.data;
                    send_response_numeric(GETDEVICE_CURRENT_HUMIDITY, OPERATION_READ, (uint32_t)device_status.environment.current_humidity);
                    break;
                }

                case IPC_CMD_SET_AUDIO_LEVEL:
                {
                    LOG_INFO(CYLF_DEF, "Rx Audio level: %u\n", ui_rx_msg_data);
                    send_response_numeric(DEVICE_AUDIO, OPERATION_READ, (uint32_t) ui_rx_msg_data);
                    break;
                }
            
                case IPC_CMD_SET_TEMP_UNIT:
                {
                    device_status.thermostat_settings.temp_unit = (temp_unit_t) ui_rx_msg_data;
                    send_response_numeric(TEMP_UNIT, OPERATION_READ,
                                          (uint32_t) device_status.thermostat_settings.temp_unit);
                    break;
                }
            
                default:
                {
                    break;
                }
            }

        }
    }
}

/*******************************************************************************
* Function Name: ui_rx_thread_init
********************************************************************************
* Summary:
*  Initializes and creates the UI receiver FreeRTOS task.
*
* Parameters:
*  void
*
* Return:
*  void
*
*******************************************************************************/
void ui_rx_thread_init(void)
{
    BaseType_t task_return = pdFAIL;

    /* Create the queue before CM55 is enabled and can deliver an IPC message. */
    xUiRxQueue = xQueueCreate(UI_RX_QUEUE_LENGTH, sizeof(ipc_msg_t));
    if (xUiRxQueue == NULL)
    {
        LOG_ERROR(CYLF_DEF, "UI RX Queue creation failed!\n");
        return;
    }

    /* Create the FreeRTOS Task */
    task_return = xTaskCreate(ui_rx_task, UI_RX_TASK_NAME, UI_RX_TASK_STACK_SIZE, NULL,
            UI_RX_TASK_PRIORITY, &cm33_ui_rx_task_handle);
    if (task_return != pdPASS)
    {
        LOG_ERROR(CYLF_DEF, "UI_RX task create Error.\n");
    }
    else
    {
        LOG_INFO(CYLF_DEF, "UI_RX task create OK.\r\n");
    }
}

/*******************************************************************************
* Function Name: is_switch_to_wifi_enabled
********************************************************************************
* Summary:
*  Returns whether forced Wi-Fi connection switch is enabled.
*
* Parameters:
*  void
*
* Return:
*  bool - true if Wi-Fi switch is enabled.
*
*******************************************************************************/
bool is_switch_to_wifi_enabled(void)
{
    return is_forced_wifi_connection;
}

/*******************************************************************************
* Function Name: set_wifi_switch_disabled
********************************************************************************
* Summary:
*  Disables the forced Wi-Fi connection switch flag.
*
* Parameters:
*  void
*
* Return:
*  void
*
*******************************************************************************/
void set_wifi_switch_disabled(void)
{
    is_forced_wifi_connection = false;
}

/* [] END OF FILE */
