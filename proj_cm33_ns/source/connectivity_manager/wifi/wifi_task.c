/*******************************************************************************
 * File Name:   wifi_task.c
 *
 * Description: This file contains the task definition for initializing the
 * Wi-Fi device, connecting to the AP, disconnecting from AP, scanning and 
 * EEPROM related functionality.
 *
 * Related Document: See Readme.md
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

#include "wifi_task.h"
#include "app_ui_receiver.h"
#include "secure_http_client.h"
#include "app_bt_gatt_handler.h"

#define SCAN_DATA_HEADER                            (2U)
#define MASK_8BITS                                  0x000000ff
#define MASK_16BITS                                 0x0000ff00
#define MASK_24BITS                                 0x00ff0000
#define MASK_32BITS                                 0xff000000
#define SHIFT_8BITS                                  (8U)
#define SHIFT_16BITS                                 (16U)
#define SHIFT_24BITS                                 (24U)
#define APP_SDIO_INTERRUPT_PRIORITY                  (7U)
#define APP_HOST_WAKE_INTERRUPT_PRIORITY             (2U)
#define APP_SDIO_FREQUENCY_HZ                        (25000000U)
#define SDHC_SDIO_64BYTES_BLOCK                      (64U)

/******************************************************************************
 * Global Variables
 ******************************************************************************/
/* WCM related structures */
cy_wcm_connect_params_t wifi_conn_param;

/* Variable to be used to store the WiFi credentials which can be copied to
 * NVRAM
 */
wifi_details_t wifi_details;

/* WiFi credentials received over BLE, pending validation/storage */
wifi_details_t wifi_cred;

/* Task Handle for WiFi Task */
TaskHandle_t wifi_task_handle;

uint8_t wifi_mac[6] = {0};

bool is_wifi_cred_need_to_update = false;
bool is_wifi_connection_cancel = false;

/* Set while the AP-connect retry loop pauses for a pending BLE scan request;
 * scan_callback() clears it and resumes the retry once the scan completes */
static volatile bool wifi_retry_paused_for_scan = false;

static mtb_hal_sdio_t sdio_instance;
static cy_stc_sd_host_context_t sdhc_host_context;
static cy_wcm_config_t wcm_config;

#define CHECK_WIFI_CONNECTION_NEED_TO_CANCEL         \
        if(is_wifi_connection_cancel){                \
            is_wifi_connection_cancel = false;        \
            if (cy_wcm_is_connected_to_ap() == 1){    \
                LOG_INFO(CYLF_DEF, "Disconnecting Wifi.....\n");\
                cy_wcm_disconnect_ap();                \
            }                                        \
            continue;                                \
        }                                            \
        
/* Interrupt config structure */

/*******************************************************************************
* Function Declaration
*******************************************************************************/
static void scan_callback(cy_wcm_scan_result_t *result_ptr, void *user_data,
                   cy_wcm_scan_status_t status);

static int start_wifi_scanning(void *notifiedvalue, cy_wcm_scan_filter_t *filter);
/*******************************************************************************
 *                          FUNCTION DEFINITIONS
 *****************************************************************************/

/*******************************************************************************
* Function Name: sdio_interrupt_handler
********************************************************************************
* Summary:
* Interrupt handler function for SDIO instance.
*******************************************************************************/
static void sdio_interrupt_handler(void)
{
    mtb_hal_sdio_process_interrupt(&sdio_instance);
}

/*******************************************************************************
* Function Name: host_wake_interrupt_handler
********************************************************************************
* Summary:
* Interrupt handler function for the host wake up input pin.
*******************************************************************************/
static void host_wake_interrupt_handler(void)
{
    mtb_hal_gpio_process_interrupt(&wcm_config.wifi_host_wake_pin);
}

/*******************************************************************************
* Function Name: wifi_event_callback
********************************************************************************
* Summary:
*  Callback function for Wi-Fi connection manager events.
*
* Parameters:
*  event - The WCM event type.
*  event_data - Pointer to event-specific data.
*
* Return:
*  void
*
*******************************************************************************/
static void wifi_event_callback(cy_wcm_event_t event, cy_wcm_event_data_t *event_data)
{
    switch (event)
    {
        case CY_WCM_EVENT_CONNECTING:
            if(cy_wcm_is_connected_to_ap() == 0)
            {
                handle_connectivity_state(STATE_WIFI_CONNECTING);
                LOG_INFO(CYLF_DEF, "<<<<<<<<<<<<<WIFI>>>>>>>>>>>>>: Connecting to AP...\n");
            }
            break;

        case CY_WCM_EVENT_CONNECTED:
            update_conn_state(DEV_ST_WIFI_CONNECTED);
            /* Wake the HTTPS task so it retries immediately instead of waiting
             * up to HTTPS_CLIENT_TASK_DELAY_MS on a fresh/new AP connection. */
            if (https_task_handle != NULL)
            {
                xTaskNotifyGive(https_task_handle);
            }
            LOG_INFO(CYLF_DEF, "<<<<<<<<<<<<<WIFI>>>>>>>>>>>>>: Connected to AP.\n");
            break;

        case CY_WCM_EVENT_CONNECT_FAILED:
            LOG_INFO(CYLF_DEF, "<<<<<<<<<<<<<WIFI>>>>>>>>>>>>>: Connection to AP failed.\n");
            break;

        case CY_WCM_EVENT_RECONNECTED:
            handle_connectivity_state(STATE_WIFI_CONNECTED);
            update_conn_state(DEV_ST_WIFI_CONNECTED);
            /* Wake the HTTPS task so it retries immediately instead of waiting
             * up to HTTPS_CLIENT_TASK_DELAY_MS after Wi-Fi reconnects. */
            if (https_task_handle != NULL)
            {
                xTaskNotifyGive(https_task_handle);
            }
            vTaskDelay(2000);
            mqtt_task_cmd_t mqtt_task_cmd = HANDLE_DISCONNECTION;
            xQueueSend(mqtt_task_q, &mqtt_task_cmd, portMAX_DELAY);
            LOG_INFO(CYLF_DEF, "<<<<<<<<<<<<<WIFI>>>>>>>>>>>>>: Reconnected to AP.\n");
            break;

        case CY_WCM_EVENT_DISCONNECTED:
            if(cy_wcm_is_connected_to_ap() == 0)
            {
                update_conn_state(DEV_ST_WIFI_DISCONNECTED);
                LOG_INFO(CYLF_DEF, "<<<<<<<<<<<<<WIFI>>>>>>>>>>>>>: Disconnected from AP %d.\n", event_data->reason);
            }
            break;

        case CY_WCM_EVENT_IP_CHANGED:
            LOG_INFO(CYLF_DEF, "<<<<<<<<<<<<<WIFI>>>>>>>>>>>>>: IP address changed.\n");
            break;

        case CY_WCM_EVENT_INITIATED_RETRY:
            LOG_INFO(CYLF_DEF, "<<<<<<<<<<<<<WIFI>>>>>>>>>>>>>: Retrying connection to AP...\n");
            break;

        case CY_WCM_EVENT_STA_JOINED_SOFTAP:
            LOG_INFO(CYLF_DEF, "<<<<<<<<<<<<<WIFI>>>>>>>>>>>>>: A station joined the SoftAP.\n");
            break;

        case CY_WCM_EVENT_STA_LEFT_SOFTAP:
            LOG_INFO(CYLF_DEF, "<<<<<<<<<<<<<WIFI>>>>>>>>>>>>>: A station left the SoftAP.\n");
            break;

        default:
            LOG_ERROR(CYLF_DEF, "<<<<<<<<<<<<<WIFI>>>>>>>>>>>>>: Unknown WCM event: %d\n", event);
            break;
    }
}
/*******************************************************************************
* Function Name: app_sdio_init
********************************************************************************
* Summary:
* This function configures and initializes the SDIO instance used in
* communication between the host MCU and the wireless device.
*******************************************************************************/
static void app_sdio_init(void)
{
    cy_rslt_t result;
    mtb_hal_sdio_cfg_t sdio_hal_cfg;
    cy_stc_sysint_t sdio_intr_cfg =
    {
        .intrSrc = CYBSP_WIFI_SDIO_IRQ,
        .intrPriority = APP_SDIO_INTERRUPT_PRIORITY
    };

    cy_stc_sysint_t host_wake_intr_cfg =
    {
            .intrSrc = CYBSP_WIFI_HOST_WAKE_IRQ,
            .intrPriority = APP_HOST_WAKE_INTERRUPT_PRIORITY
    };

    /* Initialize the SDIO interrupt and specify the interrupt handler. */
    cy_en_sysint_status_t interrupt_init_status = Cy_SysInt_Init(&sdio_intr_cfg, sdio_interrupt_handler);

    /* SDIO interrupt initialization failed. Stop program execution. */
    if(CY_SYSINT_SUCCESS != interrupt_init_status)
    {
        ERROR_CHECK(interrupt_init_status);
    }

    /* Enable NVIC interrupt. */
    NVIC_EnableIRQ(CYBSP_WIFI_SDIO_IRQ);

    /* Setup SDIO using the HAL object and desired configuration */
    result = mtb_hal_sdio_setup(&sdio_instance, &CYBSP_WIFI_SDIO_sdio_hal_config, NULL, &sdhc_host_context);

    /* SDIO setup failed. Stop program execution. */
    if(CY_RSLT_SUCCESS != result)
    {
        ERROR_CHECK(result);
    }

    /* Initialize and Enable SD HOST */
    Cy_SD_Host_Enable(CYBSP_WIFI_SDIO_HW);
    Cy_SD_Host_Init(CYBSP_WIFI_SDIO_HW, CYBSP_WIFI_SDIO_sdio_hal_config.host_config, &sdhc_host_context);
    Cy_SD_Host_SetHostBusWidth(CYBSP_WIFI_SDIO_HW, CY_SD_HOST_BUS_WIDTH_4_BIT);

    sdio_hal_cfg.frequencyhal_hz = APP_SDIO_FREQUENCY_HZ;
    sdio_hal_cfg.block_size = SDHC_SDIO_64BYTES_BLOCK;

    /* Configure SDIO */
    mtb_hal_sdio_configure(&sdio_instance, &sdio_hal_cfg);

    /* Setup GPIO using the HAL object for WIFI WL REG ON  */
    mtb_hal_gpio_setup(&wcm_config.wifi_wl_pin, CYBSP_WIFI_WL_REG_ON_PORT_NUM, CYBSP_WIFI_WL_REG_ON_PIN);

    /* Setup GPIO using the HAL object for WIFI HOST WAKE PIN  */
    mtb_hal_gpio_setup(&wcm_config.wifi_host_wake_pin, CYBSP_WIFI_HOST_WAKE_PORT_NUM, CYBSP_WIFI_HOST_WAKE_PIN);

    /* Initialize the Host wakeup interrupt and specify the interrupt handler. */
    cy_en_sysint_status_t interrupt_init_status_host_wake =  Cy_SysInt_Init(&host_wake_intr_cfg, host_wake_interrupt_handler);

    /* Host wake up interrupt initialization failed. Stop program execution. */
    if(CY_SYSINT_SUCCESS != interrupt_init_status_host_wake)
    {
        ERROR_CHECK(interrupt_init_status_host_wake);
    }

    /* Enable NVIC interrupt. */
    NVIC_EnableIRQ(CYBSP_WIFI_HOST_WAKE_IRQ);
}

/*******************************************************************************
* Function Name: wcm_init
********************************************************************************
* Summary:
*  Initializes the Wi-Fi connection manager.
*
* Parameters:
*  void
*
* Return:
*  cy_rslt_t - CY_RSLT_SUCCESS on success, error code otherwise.
*
*******************************************************************************/
cy_rslt_t wcm_init()
{
    cy_rslt_t result = CY_RSLT_SUCCESS;

    app_sdio_init();

    /* Initialize wcm */
    wcm_config.interface = CY_WCM_INTERFACE_TYPE_STA;
    wcm_config.wifi_interface_instance = &sdio_instance;

    /* Initialize WCM */
    printf("\n");
    result = cy_wcm_init(&wcm_config);
    printf("\n");
    if (CY_RSLT_SUCCESS != result)
    {
        ERROR_CHECK(result);
    }

    result = cy_wcm_register_event_callback(&wifi_event_callback);
    if (CY_RSLT_SUCCESS != result)
    {
        ERROR_CHECK(result);
    }

    return result;
}

/*******************************************************************************
* Function Name: wifi_task
********************************************************************************
* Summary:
* This function initializes the WCM module and handles task notifications
*
* Parameters
* void *
*
* Return
* void
*******************************************************************************/
void wifi_task(void * arg)
{
    cy_rslt_t result ;

    /* Variable to use for filtering WiFi scan results */
    cy_wcm_scan_filter_t scan_filter;

    /* Notification values received from other tasks */
    uint32_t ulNotifiedValue;

    /* Return status for NVM. */
    cy_rslt_t nvm_result ;

    /* WCM configuration and IP address variables */
    cy_wcm_ip_address_t ip_address;

    result = wirelessdevice_init();
    if(CY_RSLT_SUCCESS != result)
    {
        LOG_ERROR(CYLF_DEF, "Wireless module initialization failed, Error Code: %u\n", result);
/* handle_app_error(); */
        ERROR_CHECK(result);
    }

    while(true)
    {
        /* Wait for a notification */
        xTaskNotifyWait( 0x00,      /* Don't clear any notification bits on
                                       entry. */
                         UINT32_MAX, /* Reset the notification value to 0 on
                                        exit. */
                         &ulNotifiedValue, /* Notified value pass out in
                                              ulNotifiedValue. */
                         portMAX_DELAY );  /* Block indefinitely. */

        /* Check if the notification value has the scan bit set. In some cases both
         * the scan bit and connect bit are set. In both the cases we start with
         * the scan, pass the notification value to the scan callback and if
         * the connect bit is also set then the scan callback will pass the
         * connect message to this task and this task will try and connect to
         * the WiFi network. If the notification value passed to the scan
         * callback contains only the scan bit then it will try and send
         * GATT notifications to the BLE client
         */
        if(NOTIF_SCAN & ulNotifiedValue)
        {
            memset(&scan_filter, 0, sizeof(cy_wcm_scan_filter_t));

            /* If the Connect bit is set then we need to only scan for networks
             * with the given SSID. Set the correct filter values and start scan
             */
            if(NOTIF_CONNECT & ulNotifiedValue)
            {
                /* Configure the scan filter for SSID */
                scan_filter.mode = CY_WCM_SCAN_FILTER_TYPE_SSID;
                memcpy((char *)scan_filter.param.SSID,(char *)wifi_conn_param.ap_credentials.SSID,
                strlen((char *)wifi_conn_param.ap_credentials.SSID) + 1);
                LOG_INFO(CYLF_DEF, "Starting scan with SSID: %s\n", scan_filter.param.SSID);

                if(CY_RSLT_WCM_SCAN_IN_PROGRESS == start_wifi_scanning(&ulNotifiedValue, &scan_filter))
                {
                    LOG_INFO(CYLF_DEF, "Scan is already in progress, then try to connect with Wifi......\r\n");
                    xTaskNotify(wifi_task_handle, NOTIF_CONNECT, eSetValueWithOverwrite);
                }
            }
            /* Else start scan without any filter */
            else
            {
                if(CY_RSLT_WCM_SCAN_IN_PROGRESS == start_wifi_scanning(&ulNotifiedValue, NULL))
                {
                    LOG_INFO(CYLF_DEF, "Scan is already in progress, then try to connect with Wifi......\r\n");
                    xTaskNotify(wifi_task_handle, NOTIF_CONNECT, eSetValueWithOverwrite);
                }
            }
        }

        /* Check for connection notification */
        else if(NOTIF_CONNECT == ulNotifiedValue)
        {
            /* Variable to track the number of connection retries to the Wi-Fi
             * AP
             */
            uint8_t conn_retries = 0;

            if( (cy_wcm_is_connected_to_ap() == true) && (!is_credentials_changed()) )
            {
                 mqtt_task_cmd_t mqtt_task_cmd = HANDLE_CONNECT;

                 /* Keep state machine consistent with the fresh-join path
                  * so it reflects Wi-Fi connected before the MQTT attempt.
                  * (No update_conn_state() here - it shares a single-slot IPC
                  * dedupe cache with other conn-state UI updates such as BLE
                  * advertising, and racing with those clobbers the UI.) */
                 handle_connectivity_state(STATE_WIFI_CONNECTED);

                 LOG_INFO(CYLF_DEF, "Wi-Fi is already connected using the given credentials.\r\n");

                 xQueueSend(mqtt_task_q, &mqtt_task_cmd, portMAX_DELAY);

                 /* Update GATT DB about connection */
                 app_custom_service_wifi_control[0] = WIFI_CONTROL_CONNECT;

                 /* Check if the connection is active and notifications are
                  * enabled and then send the notification
                  */
                 if ((conn_id) &&
                    ((app_custom_service_wifi_control_client_char_config[0] &
                      GATT_CLIENT_CONFIG_NOTIFICATION)))
                 {
                     uint8_t *p_attr = (uint8_t*)app_custom_service_wifi_control;

                     /* Wait if GATT congestion flag enabled. */
                     while (is_gatt_congested);

                     wiced_bt_gatt_server_send_notification(conn_id,
                                     HDLC_CUSTOM_SERVICE_WIFI_CONTROL_VALUE,
                                     sizeof(app_custom_service_wifi_control[0]),
                                     p_attr, NULL);

                     /* Wait for GATT congestion to clear */
                     while (is_gatt_congested);
                 }

                 continue;
            }
            /* Join the Wi-Fi AP */
            result = CY_RSLT_TYPE_ERROR;
            wifi_retry_paused_for_scan = false;

            while((CY_RSLT_SUCCESS != result)
                    && ((get_retry_state(WIFI_INTERFACE) == WIFI_CONN_RETRY_INFINITE) || (conn_retries++ < MAX_CONNECTION_RETRIES))
                    && (!is_wifi_connection_cancel))
            {
                /* Peek (non-destructive) for a pending BLE scan request - either a
                 * standalone "list networks" scan, or a fresh connect request with
                 * new credentials (which always re-scans for its SSID first). Checked
                 * before touching wifi_conn_param so a credential update that lands
                 * during the previous attempt's retry delay can't be picked up half
                 * paired with the old AP's scan-derived security/BSSID.
                 * Uses ulTaskNotifyValueClear() rather than xTaskNotifyWait(), since the
                 * latter clears the pending notification state even with 0 clear-masks. */
                uint32_t pending_notif = ulTaskNotifyValueClear(NULL, 0);

                if (NOTIF_SCAN & pending_notif)
                {
                    LOG_INFO(CYLF_DEF, "Pausing AP-connect retry for a pending BLE scan request.\n");
                    wifi_retry_paused_for_scan = true;
                    break;
                }

                LOG_INFO(CYLF_DEF, "Trying to connect SSID: %s at %s\n",
                        wifi_conn_param.ap_credentials.SSID,
                        __FUNCTION__);
                
                /* Connect to the given AP */
                result = cy_wcm_connect_ap(&wifi_conn_param, &ip_address);

                if (CY_RSLT_SUCCESS != result)
                {
                    vTaskDelay(pdMS_TO_TICKS(500));
                }
            }

            if (wifi_retry_paused_for_scan)
            {
                /* Nothing to report yet - scan_callback() reposts NOTIF_CONNECT once
                 * the pending scan finishes, which resumes this loop from the top. */
            }
            else if(CY_RSLT_SUCCESS == result)
            {
                CHECK_WIFI_CONNECTION_NEED_TO_CANCEL;

                LOG_INFO(CYLF_DEF, "Successfully joined the Wi-Fi network\n");
                display_ble_pin(0);

                /* Store WiFi credentials in NVM */
                /* Write data to NVM. */
                is_credentials_need_store();

                handle_connectivity_state(STATE_WIFI_CONNECTED);
                vTaskDelay(100);
                update_conn_state(DEV_ST_WIFI_CONNECTED);
                vTaskDelay(2000);

                /* Update GATT DB about connection */
                app_custom_service_wifi_control[0] = WIFI_CONTROL_CONNECT;

                /* Check if the connection is active and  notifications are
                 * enabled and then send the notification
                 */
                if ((conn_id) &&
                   ((app_custom_service_wifi_control_client_char_config[0] &
                     GATT_CLIENT_CONFIG_NOTIFICATION)))
                {
                    uint8_t *p_attr = (uint8_t*)app_custom_service_wifi_control;

                    /* Wait if GATT congestion flag enabled. */
                    while (is_gatt_congested);

                    wiced_bt_gatt_server_send_notification(conn_id,
                                    HDLC_CUSTOM_SERVICE_WIFI_CONTROL_VALUE,
                                    sizeof(app_custom_service_wifi_control[0]),
                                    p_attr, NULL);

                    /* Wait for GATT congestion to clear */
                    while (is_gatt_congested);
                }
                else /* Notification not sent */
                {
                    LOG_INFO(CYLF_DEF, "Notification not sent\n");
                }
                
                if(is_wifi_cred_need_to_update)
                {
                memset(&wifi_details, 0, sizeof(wifi_details));
                   
                /* Copy SSID */
                uint8_t ssid_len = strlen((const char *)wifi_conn_param.ap_credentials.SSID);
                if (ssid_len > CY_WCM_MAX_SSID_LEN)
                {
                    ssid_len = CY_WCM_MAX_SSID_LEN;
                }
                memcpy(wifi_details.wifi_ssid, wifi_conn_param.ap_credentials.SSID, ssid_len);
                wifi_details.ssid_len = ssid_len;
    
                   /* Copy Password */
               uint8_t password_len = strlen((const char *)wifi_conn_param.ap_credentials.password);
               if (password_len > CY_WCM_MAX_PASSPHRASE_LEN)
               {
                   password_len = CY_WCM_MAX_PASSPHRASE_LEN;
               }
               memcpy(wifi_details.wifi_password, wifi_conn_param.ap_credentials.password, password_len);
               wifi_details.password_len = password_len;
                    
                   is_wifi_cred_need_to_update = false;
        }

                mqtt_task_cmd_t mqtt_task_cmd = HANDLE_CONNECT;
                xQueueSend(mqtt_task_q, &mqtt_task_cmd, portMAX_DELAY);
            }
            else /* WiFi connection failed */
            {
        if(is_wifi_cred_need_to_update)
            {
                    memset(&wifi_conn_param, 0, sizeof(cy_wcm_connect_params_t));
            memcpy(wifi_conn_param.ap_credentials.SSID, wifi_details.wifi_ssid, wifi_details.ssid_len);
            memcpy(wifi_conn_param.ap_credentials.password, wifi_details.wifi_password, wifi_details.password_len);    
                    is_wifi_cred_need_to_update = false;
                    
                    
                    xTaskNotify(wifi_task_handle, (NOTIF_SCAN | NOTIF_CONNECT), eSetValueWithOverwrite);
        }
                
                CHECK_WIFI_CONNECTION_NEED_TO_CANCEL;

                /* Update GATT DB about unsuccessful connection */
                app_custom_service_wifi_control[0] = WIFI_CONTROL_DISCONNECT;

                handle_connectivity_state(STATE_WIFI_DISCONNECTED);

                /* Send notification for unsuccessful connection */
                /* Check if the connection is active and notifications are
                 * enabled and then send the notification
                 */
                if ((conn_id) &&
                    ((app_custom_service_wifi_control_client_char_config[0] &
                      GATT_CLIENT_CONFIG_NOTIFICATION)))
                {
                    uint8_t *p_attr = (uint8_t*)app_custom_service_wifi_control;

                    /* Wait if GATT congestion flag enabled. */
                    while (is_gatt_congested);

                    wiced_bt_gatt_server_send_notification(conn_id,
                                     HDLC_CUSTOM_SERVICE_WIFI_CONTROL_VALUE,
                                     sizeof(app_custom_service_wifi_control[0]),
                                     p_attr, NULL);

                    /* Wait for GATT congestion to clear */
                    while (is_gatt_congested);
                }
                else /* Notification not sent */
                {
                    LOG_INFO(CYLF_DEF, "Notification not sent\n");
                }

                LOG_INFO(CYLF_DEF, "Failed to join Wi-Fi network\n");

                /* Check if BLE connected, update the screen to BLE */
                if(conn_id != false)
                {
                    LOG_INFO(CYLF_DEF, "WiFi not connected. Update UI to BLE conn.\n");
                    update_conn_state(DEV_ST_BLE_CONNECTED);
                }
                else
                {
                    LOG_INFO(CYLF_DEF, "WiFi &  BLE not connected. Update UI to BLE ADV.\n");
                    set_wifi_switch_disabled();
                    handle_connectivity_state(STATE_BLE_ADV);
                }
            }
        }
        /* Task notification for disconnection */
        else if(NOTIF_DISCONNECT & ulNotifiedValue)
        {
            LOG_INFO(CYLF_DEF, "Disconnecting Wi-Fi\n");
            result = cy_wcm_disconnect_ap();
            if(CY_RSLT_SUCCESS == result)
            {
                /* Update GATT DB about disconnection */
                app_custom_service_wifi_control[0] = WIFI_CONTROL_DISCONNECT;

                LOG_INFO(CYLF_DEF, "Successfully disconnected from AP\n");

                if( !(NOTIF_ERASE_DATA & ulNotifiedValue) )
                {
                    handle_connectivity_state(STATE_WIFI_DISCONNECTED);
                }
            }
            else /* Disconnection failed */
            {
                LOG_INFO(CYLF_DEF, "Failed to disconnect\n");
            }

            /* Check if the erase bit is also set in the notification. If yes
            * then erase the data from NVM
            */
            if(NOTIF_ERASE_DATA & ulNotifiedValue)
            {
                LOG_INFO(CYLF_DEF, "Deleting Wi-Fi data from NVM\n");

                /* Set the data to 0*/
                memset(&wifi_details, 0, sizeof(wifi_details));

                nvm_result =  Cy_RRAM_WriteByteArray(RRAMC0, APP_RRAM_NVM_MAIN_NS_START + RRAM_NVM_WIFI_CRED_OFFSET , &wifi_details.wifi_ssid[0],sizeof(wifi_details));
                if (nvm_result)
                {
                    LOG_ERROR(CYLF_DEF, "Failed to write to NVM\n");
                }
                else
                {
                    device_provisioned = false;
                    update_provision_state(device_provisioned);
                }

                /* Set the WiFi Connection parameters structure to 0 */
                memset(&wifi_conn_param, 0, sizeof(cy_wcm_connect_params_t));
                
                vTaskDelay(2000);
                handle_connectivity_state(STATE_UNPROVISIONED);
                
                /* Stop ongoing connection requests if any */
                update_credential = false;
            }

            if(update_credential)
            {
                xTaskNotify(wifi_task_handle, (NOTIF_SCAN | NOTIF_CONNECT), eSetValueWithOverwrite);
                update_credential = false;
            }
            else if( true == is_wifi_connection_cancel)
            {
                is_wifi_connection_cancel = false;
            }
        }
        else if(cy_wcm_is_connected_to_ap() == true)
        {
            LOG_INFO(CYLF_DEF, "Wi-Fi already connected\n");
            if(get_mqtt_status() & FLAG_MQTT_CONNECTION_SUCCESS)
            {
                update_conn_state(DEV_ST_CLOUD_CONNECTED);
            }
            else
            {
                mqtt_task_cmd_t mqtt_task_cmd = HANDLE_CONNECT;
                xQueueSend(mqtt_task_q, &mqtt_task_cmd, portMAX_DELAY);
            }
        }
    }
}

/*******************************************************************************
* Function Name: wifi_connect
********************************************************************************
* Summary:
*  Attempts to connect to the configured Wi-Fi access point with retries.
*
* Parameters:
*  void
*
* Return:
*  cy_rslt_t - CY_RSLT_SUCCESS on success, error code otherwise.
*
*******************************************************************************/
cy_rslt_t wifi_connect(void)
{
    cy_wcm_ip_address_t ip_address;
    cy_rslt_t result = CY_RSLT_TYPE_ERROR;
    uint8_t conn_retries = 0;

    handle_connectivity_state(STATE_WIFI_CONNECTING);

    if(CY_WCM_SECURITY_OPEN == wifi_conn_param.ap_credentials.security)
    {
        wifi_conn_param.ap_credentials.security = CY_WCM_SECURITY_UNKNOWN;
    }

    wifi_conn_param.band         = CY_WCM_WIFI_BAND_ANY;
    wifi_conn_param.itwt_profile = CY_WCM_ITWT_PROFILE_NONE;

    while((CY_RSLT_SUCCESS != result)
            && ((get_retry_state(WIFI_INTERFACE) == WIFI_CONN_RETRY_INFINITE) || (conn_retries < MAX_CONNECTION_RETRIES))
            && (!is_wifi_connection_cancel))
    {
        LOG_INFO(CYLF_DEF, "Trying to connect SSID: %s at %s\n",
                wifi_conn_param.ap_credentials.SSID,
                __FUNCTION__);

        /* Connect to the given AP */
        result = cy_wcm_connect_ap(&wifi_conn_param, &ip_address);
        conn_retries++;

        vTaskDelay(500);
    }

    return result;
}

/*******************************************************************************
 * Function Name: scan_callback
 *******************************************************************************
 * Summary: The callback function which accumulates the scan results.
 * If the CONNECT bit is set in the user_data then send back task notification
 * to the WiFi task to connect to the AP. If not then send the results back to
 * the BLE Client
 *
 * Parameters:
 *  cy_wcm_scan_result_t *result_ptr: Pointer to the scan result
 *  void *user_data: User data.
 *  cy_wcm_scan_status_t status: Status of scan completion.
 *
 * Return:
 *  void
 *
 ******************************************************************************/
static void scan_callback(cy_wcm_scan_result_t *result_ptr, void *user_data,
                   cy_wcm_scan_status_t status)
{
    uint8_t *scan_data;
    uint8_t byte_no = 0;
    uint8_t ssid_len = 0;

    if(NULL != result_ptr)
    {
        ssid_len = strlen((const char *)result_ptr->SSID);
    }

    if ((ssid_len != 0) && (CY_WCM_SCAN_INCOMPLETE == status))
    {
        /* Only record security/BSSID for a connect-intent (SSID-filtered) scan;
         * an unfiltered "list networks" scan must not clobber the connect params
         * mid-retry with an unrelated AP's fields */
        if (NOTIF_CONNECT & *(uint32_t *) user_data)
        {
            wifi_conn_param.ap_credentials.security = result_ptr->security;
            /* Carry over the BSSID found during scan so cy_wcm_connect_ap() can join
             * this specific AP directly instead of re-scanning for the SSID */
            memcpy(wifi_conn_param.BSSID, result_ptr->BSSID, CY_WCM_MAC_ADDR_LEN);
        }
        LOG_DEBUG(CYLF_DEF, "%-32s\t", result_ptr->SSID);
        LOG_DEBUG(CYLF_DEF, "%s\n", get_wifi_security_name(result_ptr->security));

        /* If scan bit is set then send the scan result to the BLE Client */
        if(NOTIF_SCAN == *(uint32_t *) user_data)
        {

            if ((conn_id != 0) &&
                (app_custom_service_wifi_networks_client_char_config[0] &
                GATT_CLIENT_CONFIG_NOTIFICATION))
            {
                /* Get memory for the buffer to send the scan data */
                scan_data = (uint8_t *)wiced_bt_get_buffer(SCAN_DATA_HEADER + ssid_len + SCAN_DATA_HEADER + sizeof(uint32_t));
                if(NULL == scan_data)
                {
                    LOG_INFO(CYLF_DEF, "Buffer for notification not allocated\n");
                    return;
                }
                /* Fill in the SSID first */
                scan_data[byte_no++] = SCAN_PACKET_TYPE_SSID;
                scan_data[byte_no++] = ssid_len;
                memcpy(&scan_data[byte_no], (const char *)result_ptr->SSID, ssid_len);
                byte_no = byte_no + ssid_len;

                /* Fill in the 4 bytes of security value */
                scan_data[byte_no++] = SCAN_PACKET_TYPE_SECURITY;
                scan_data[byte_no++] = sizeof(uint32_t);
                scan_data[byte_no++] = (uint8_t)(result_ptr->security & MASK_8BITS);
                scan_data[byte_no++] = (uint8_t)((result_ptr->security &
                                                  MASK_16BITS) >> SHIFT_8BITS);
                scan_data[byte_no++] = (uint8_t)((result_ptr->security &
                                                  MASK_24BITS) >> SHIFT_16BITS);
                scan_data[byte_no++] = (uint8_t)((result_ptr->security &
                                                  MASK_32BITS) >> SHIFT_24BITS);

                /* Wait if GATT congestion flag enabled. */
                while (is_gatt_congested);

                wiced_bt_gatt_server_send_notification(conn_id,
                        HDLC_CUSTOM_SERVICE_WIFI_NETWORKS_VALUE, byte_no, scan_data,
                    (wiced_bt_gatt_app_context_t)wiced_bt_free_buffer);

                /* Wait for GATT congestion to clear */
                while (is_gatt_congested);
            }
        }
    }

    /* If the connect bit is set in the user data then connect notification to
     * the WiFi task
     */
    if ((CY_WCM_SCAN_COMPLETE == status) && ((NOTIF_SCAN | NOTIF_CONNECT) ==
                                              *(uint32_t *) user_data))
    {
        xTaskNotify(wifi_task_handle, NOTIF_CONNECT, eSetValueWithOverwrite);
    }
    /* Standalone scan completed - resume an AP-connect retry that paused for it */
    else if ((CY_WCM_SCAN_COMPLETE == status) && (NOTIF_SCAN == *(uint32_t *) user_data)
            && wifi_retry_paused_for_scan)
    {
        wifi_retry_paused_for_scan = false;
        xTaskNotify(wifi_task_handle, NOTIF_CONNECT, eSetBits);
    }
}

/*******************************************************************************
* Function Name: start_wifi_scanning
********************************************************************************
* Summary:
*  Initiates a Wi-Fi scan operation.
*
* Parameters:
 *  notifiedvalue - Pointer to notification value for scan callback.
 *  filter - Pointer to scan filter, or NULL for unfiltered scan.
 *
 * Return:
 *  int - CY_RSLT_SUCCESS on success, error code otherwise.
*
*******************************************************************************/
static int start_wifi_scanning(void *notifiedvalue, cy_wcm_scan_filter_t *filter)
{
    cy_rslt_t result = 0;

    result = cy_wcm_start_scan(scan_callback, notifiedvalue, filter);

    if(CY_RSLT_SUCCESS != result)
    {
        LOG_ERROR(CYLF_DEF, "Start scan failed error: %x\n", result);
        return result;
    }
    
    return result;
}

/* [] END OF FILE */

