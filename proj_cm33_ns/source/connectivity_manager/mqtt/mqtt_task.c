/*******************************************************************************
 * File Name:   mqtt_task.c
 *
 * Description: This file contains the task that handles initialization &
 *              connection of Wi-Fi and the MQTT client. The task then starts
 *              the subscriber and the publisher tasks. The task also implements
 *              reconnection mechanisms to handle WiFi and MQTT disconnections.
 *              The task also handles all the cleanup operations to gracefully
 *              terminate the Wi-Fi and MQTT connections in case of any failure.
 *
 * Related Document: See README.md
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
#include <stdbool.h>
#include "cybsp.h"

/* FreeRTOS header files */
#include "FreeRTOS.h"
#include "ipc_communication.h"
#include "task.h"

/* Task header files */
#include "mqtt_task.h"
#include "subscriber_task.h"
#include "publisher_task.h"

/* Configuration file for Wi-Fi and MQTT client */
#include "mqtt_client_config.h"

/* Middleware libraries */
#include "cy_retarget_io.h"
#include "cy_wcm.h"

#include "cy_mqtt_api.h"
#include "clock.h"

/* LwIP header files */
#include "lwip/netif.h"
#include "retarget_io_init.h"

#include "wireless_manager.h"
#include "mqtt/mqtt_command_handler.h"
#include "wifi_task.h"
#include "app_bt_bonding.h"
#include "app_bt_event_handler.h"
#include "app_bt_gatt_handler.h"
/*******************************************************************************
 *                                MACROS
 *****************************************************************************/
/* Queue length of a message queue that is used to communicate the status of 
 * various operations.
 */
#define MQTT_TASK_QUEUE_LENGTH           (10u)

/* Time in milliseconds to wait before creating the publisher task. */
#define TASK_CREATION_DELAY_MS           (2000u)

#define TIME_DIV_MS                                 (60000u)
#define APP_SDIO_INTERRUPT_PRIORITY                 (7U)
#define APP_HOST_WAKE_INTERRUPT_PRIORITY            (2U)
#define APP_SDIO_FREQUENCY_HZ                       (25000000U)
#define SDHC_SDIO_64BYTES_BLOCK                     (64U)


#define MQTT_PUBLISH_MAX_WAIT_TIME_IN_MS            (3000)

/*String that describes the MQTT handle that is being created in order to uniquely identify it*/
#define MQTT_HANDLE_DESCRIPTOR                       "MQTThandleID"

/* Macro to check if the result of an operation was successful and set the 
 * corresponding bit in the status_flag based on 'init_mask' parameter. When 
 * it has failed, print the error message and return the result to the 
 * calling function.
 */
#define CHECK_RESULT(result, init_mask, error_message...)      \
                     do                                        \
                     {                                         \
                         if ((int)result == CY_RSLT_SUCCESS)   \
                         {                                     \
                             status_flag |= init_mask;         \
                         }                                     \
                         else                                  \
                         {                                     \
                             LOG_ERROR(CYLF_DEF, error_message);            \
                             return result;                    \
                         }                                     \
                     } while(0)

#define CHECK_MQTT_CONNECTION_NEED_TO_CANCEL                                                          \
        if(is_cloud_connection_cancel){                                                                 \
            LOG_INFO(CYLF_DEF, "Manual Disconnect at  CHECK_MQTT_CONNECTION_NEED_TO_CANCEL\r\n");    \
            mqtt_task_cmd_t mqtt_cmd = HANDLE_MANUAL_DISCONNECTION;                                  \
            xQueueSend(mqtt_task_q, &mqtt_cmd, portMAX_DELAY);                                       \
            is_cloud_connection_cancel = false;                                                         \
            continue;                                                                                 \
        }                                                                                             \

/*******************************************************************************
 *                              GLOBAL VARIABLES
 *****************************************************************************/
static mqtt_task_cmd_t mqtt_status;

static bool mqtt_init_status = false;

/* MQTT connection handle. */
cy_mqtt_t mqtt_connection;

/* Queue handle used to communicate results of various operations - MQTT 
 * Publish, MQTT Subscribe, MQTT connection, and Wi-Fi connection between tasks 
 * and callbacks.
 */
QueueHandle_t mqtt_task_q;

/* Flag to denote initialization status of various operations. */
uint32_t status_flag = 0;

/* Pointer to the network buffer needed by the MQTT library for MQTT send and 
 * receive operations.
 */
uint8_t *mqtt_network_buffer = NULL;

mqtttopic_t mqtt_topics[NUMBERS_OF_TOPIC] = {0};

bool is_cloud_connection_cancel = false;

/* Mutex to guard the send response */
SemaphoreHandle_t uplink_mutex = NULL;
/*******************************************************************************
 *                          FUNCTION PROTOTYPES
 *****************************************************************************/

#if GENERATE_UNIQUE_CLIENT_ID
static cy_rslt_t mqtt_get_unique_client_identifier(char *mqtt_client_identifier);
#endif /* GENERATE_UNIQUE_CLIENT_ID */


/******************************************************************************
 * Function Name: cleanup
 ******************************************************************************
 * Summary:
 *  Function that invokes the deinit and cleanup functions for various
 *  operations based on the status_flag.
 *
 * Parameters:
 *  void
 *
 * Return:
 *  void
 *
 ******************************************************************************/
void cleanup_mqtt(void)
{
    cy_rslt_t status = CY_RSLT_SUCCESS;

    subscriber_data_t subscriber_q_data;
    subscriber_q_data.cmd = UNSUBSCRIBE_FROM_TOPIC;
    xQueueSend(subscriber_task_q, &subscriber_q_data, portMAX_DELAY);

    vTaskDelay(pdMS_TO_TICKS(1000));
    
    /* Disconnect the MQTT connection if it was established. */
    if (status_flag & FLAG_MQTT_CONNECTION_SUCCESS)
    {
        mqtt_disconnect();
    }
    /* Delete the MQTT instance if it was created. */
    if (status_flag & FLAG_MQTT_INSTANCE_CREATED)
    {
        status = cy_mqtt_delete(mqtt_connection);

        if (CY_RSLT_SUCCESS == status)
        {
            LOG_INFO(CYLF_DEF, "Removed MQTT connection info from stack...\n");
        }
        else
        {
            LOG_INFO(CYLF_DEF, "MQTT delete API failed unexpectedly.\n");
        }
    }
    /* Deallocate the network buffer. */
    if (status_flag & FLAG_BUFFER_INITIALIZED)
    {
        vPortFree((void *) mqtt_network_buffer);
    }
    /* Deinit the MQTT library. */
    if (status_flag & FLAG_LIBS_INITIALIZED)
    {
        status = cy_mqtt_deinit();

        if (CY_RSLT_SUCCESS == status)
        {
            LOG_INFO(CYLF_DEF, "Deinitialized MQTT stack...\n");
        }
        else
        {
            LOG_INFO(CYLF_DEF, "MQTT deinit API failed unexpectedly.\n");
        }
    }

    vTaskDelay(pdMS_TO_TICKS(1000));
    mqtt_init_status = false;
}

/******************************************************************************
 * Function Name: mqtt_event_callback
 ******************************************************************************
 * Summary:
 *  Callback invoked by the MQTT library for events like MQTT disconnection,
 *  incoming MQTT subscription messages from the MQTT broker.
 *    1. In case of MQTT disconnection, the MQTT client task is communicated
 *       about the disconnection using a message queue.
 *    2. When an MQTT subscription message is received, the subscriber callback
 *       function implemented in subscriber_task.c is invoked to handle the
 *       incoming MQTT message.
 *
 * Parameters:
 *  cy_mqtt_t mqtt_handle : MQTT handle corresponding to the MQTT event (unused)
 *  cy_mqtt_event_t event : MQTT event information
 *  void *user_data : User data pointer passed during cy_mqtt_create() (unused)
 *
 * Return:
 *  void
 *
 ******************************************************************************/
static void mqtt_event_callback(cy_mqtt_t mqtt_handle, cy_mqtt_event_t event, void *user_data)
{
    cy_mqtt_publish_info_t *received_msg;
    mqtt_task_cmd_t mqtt_task_cmd;

    CY_UNUSED_PARAMETER(mqtt_handle);
    CY_UNUSED_PARAMETER(user_data);

    LOG_INFO(CYLF_DEF, "\nMQTT Received Event: %d!\n",event.type);

    switch(event.type)
    {
        case CY_MQTT_EVENT_TYPE_DISCONNECT:
        {
            /* Send connection status to UI */
            update_conn_state(DEV_ST_CLOUD_DISCONNECTED);
            vTaskDelay(2000);

            /* Clear the status flag bit to indicate MQTT disconnection. */
            status_flag &= ~(FLAG_MQTT_CONNECTION_SUCCESS);

            /* MQTT connection with the MQTT broker is broken as the client
             * is unable to communicate with the broker. Set the appropriate
             * command to be sent to the MQTT task.
             */
            LOG_INFO(CYLF_DEF, "\nUnexpectedly disconnected from MQTT broker!\n");
            mqtt_task_cmd = HANDLE_DISCONNECTION;

            /* Send the message to the MQTT client task to handle the
             * disconnection.
             */
            xQueueSend(mqtt_task_q, &mqtt_task_cmd, portMAX_DELAY);
            break;
        }

        case CY_MQTT_EVENT_TYPE_SUBSCRIPTION_MESSAGE_RECEIVE:
        {
            status_flag |= FLAG_MQTT_MSG_RECEIVED;

            /* Incoming MQTT message has been received. Send this message to
             * the subscriber callback function to handle it.
             */

            received_msg = &(event.data.pub_msg.received_message);

            mqtt_subscription_callback(received_msg);
            break;
        }
        default :
        {
            /* Unknown MQTT event */
            LOG_ERROR(CYLF_DEF, "\nUnknown Event received from MQTT callback!\n");
            break;
        }
    }
}

/******************************************************************************
 * Function Name: mqtt_init
 ******************************************************************************
 * Summary:
 *  Function that initializes the MQTT library and creates an instance for the
 *  MQTT client. The network buffer needed by the MQTT library for MQTT send
 *  send and receive operations is also allocated by this function.
 *
 * Parameters:
 *  void
 *
 * Return:
 *  cy_rslt_t : CY_RSLT_SUCCESS on a successful initialization, else an error
 *              code indicating the failure.
 *
 ******************************************************************************/
static cy_rslt_t mqtt_init(void)
{
    /* Variable to indicate status of various operations. */
    cy_rslt_t result = CY_RSLT_SUCCESS;

    /* Initialize the MQTT library. */
    result = cy_mqtt_init();
    CHECK_RESULT(result, FLAG_LIBS_INITIALIZED, "\nMQTT library initialization failed!\n");

    /* Allocate buffer for MQTT send and receive operations. */
    mqtt_network_buffer = (uint8_t *) pvPortMalloc(sizeof(uint8_t) * MQTT_NETWORK_BUFFER_SIZE);
    if(NULL == mqtt_network_buffer)
    {
        result = ~CY_RSLT_SUCCESS;
    }
    CHECK_RESULT(result, FLAG_BUFFER_INITIALIZED, "Network Buffer allocation failed!\n\n");

    /* Create the MQTT client instance. */
    result = cy_mqtt_create(mqtt_network_buffer, MQTT_NETWORK_BUFFER_SIZE,
                            security_info, &broker_info,MQTT_HANDLE_DESCRIPTOR,
                            &mqtt_connection);        

    CHECK_RESULT(result, FLAG_MQTT_INSTANCE_CREATED, "\nMQTT instance creation failed!\n");
    if(CY_RSLT_SUCCESS == result)
    {
        /* Register a MQTT event callback */
        result = cy_mqtt_register_event_callback( mqtt_connection, (cy_mqtt_callback_t)mqtt_event_callback, NULL );
        if(CY_RSLT_SUCCESS == result)
        {
            LOG_INFO(CYLF_DEF, "\nMQTT library initialization successful.\n");
        }
    }
    return result;
}

/******************************************************************************
 * Function Name: mqtt_connect
 ******************************************************************************
 * Summary:
 *  Function that initiates MQTT connect operation. The connection is retried
 *  a maximum of 'MAX_MQTT_CONN_RETRIES' times with interval of
 *  'MQTT_CONN_RETRY_INTERVAL_MS' milliseconds.
 *
 * Parameters:
 *  void
 *
 * Return:
 *  cy_rslt_t : CY_RSLT_SUCCESS upon a successful MQTT connection, else an
 *              error code indicating the failure.
 *
 ******************************************************************************/
static cy_rslt_t mqtt_connect(void)
{
    /* Variable to indicate status of various operations. */
    cy_rslt_t result = CY_RSLT_SUCCESS;
    bool mqtt_conn_status;
    uint8_t conn_retries = 0;

    /* Broker endpoint is still the unedited placeholder - skip connecting/
     * retrying since it can never succeed. */
    if (0 == strcmp(broker_info.hostname, "ENTER_BROKER_ENDPOINT_HERE"))
    {
        LOG_ERROR(CYLF_DEF, "\nMQTT broker endpoint is not configured (placeholder value). "
               "Skipping MQTT connect.\n\n");
        return CY_RSLT_TYPE_ERROR;
    }

    if(false == mqtt_init_status)
    {
        result = mqtt_init();
        if(CY_RSLT_SUCCESS == result)
        {
            mqtt_init_status = true;
        }
        else
        {
            LOG_INFO(CYLF_DEF, "\nMQTT Init failed with error 0x%0X\n\n",
                   (int)result);
        }
    }
    
    /* MQTT client identifier string. */
    char mqtt_client_identifier[(MQTT_CLIENT_IDENTIFIER_MAX_LEN + 1)] = MQTT_CLIENT_IDENTIFIER;

    update_conn_state(DEV_ST_CLOUD_CONNECTING);

    /* Configure the user credentials as a part of MQTT Connect packet */
    if (strlen(MQTT_USERNAME) > 0)
    {
        connection_info.username = MQTT_USERNAME;
        connection_info.password = MQTT_PASSWORD;
        connection_info.username_len = sizeof(MQTT_USERNAME) - 1;
        connection_info.password_len = sizeof(MQTT_PASSWORD) - 1;
    }

    /* Generate a unique client identifier with 'MQTT_CLIENT_IDENTIFIER' string
     * as a prefix if the `GENERATE_UNIQUE_CLIENT_ID` macro is enabled.
     */
#if GENERATE_UNIQUE_CLIENT_ID
    result = mqtt_get_unique_client_identifier(mqtt_client_identifier);
    CHECK_RESULT(result, 0, "Failed to generate unique client identifier for the MQTT client!\n");
#endif /* GENERATE_UNIQUE_CLIENT_ID */

    /* Set the client identifier buffer and length. */
    connection_info.client_id = mqtt_client_identifier;
    connection_info.client_id_len = strlen(mqtt_client_identifier);

    LOG_INFO(CYLF_DEF, "\n'%.*s' connecting to MQTT broker '%.*s'...\n",
           connection_info.client_id_len,
           connection_info.client_id,
           broker_info.hostname_len,
           broker_info.hostname);

    connection_info.will_info->topic = mqtt_topics[1];
    connection_info.will_info->topic_len = MQTT_TOPIC_SIZE;
    connection_info.will_info->payload = "{\"cmd_id\":0,\"type\":0,\"value\":0}";
    connection_info.will_info->payload_len = (size_t)(strlen(connection_info.will_info->payload));

    mqtt_conn_status = false;
    while((false == mqtt_conn_status)
            && ((get_retry_state(CLOUD_INTERFACE) == CLOUD_CONN_RETRY_INFINITE) || (conn_retries++ < MAX_CONNECTION_RETRIES))
            && (!is_cloud_connection_cancel))
    {
        if (cy_wcm_is_connected_to_ap() == 0)
        {
            LOG_INFO(CYLF_DEF, "\nUnexpectedly disconnected from Wi-Fi network!, Initiating Wi-Fi reconnection...\n");
            status_flag &= ~(FLAG_WIFI_CONNECTED);

            /* Initiate Wi-Fi reconnection. */
            result = wifi_connect();
            if (CY_RSLT_SUCCESS != result)
            {
                return result;
            }
        }

        /* Establish the MQTT connection. */
        result = cy_mqtt_connect(mqtt_connection, &connection_info);
        if (CY_RSLT_SUCCESS == result)
        {
            LOG_INFO(CYLF_DEF, "MQTT connection successful.\r\n");

            /* Set the appropriate bit in the status_flag to denote successful
             * MQTT connection, and return the result to the calling function.
             */
            status_flag |= FLAG_MQTT_CONNECTION_SUCCESS;
            mqtt_conn_status = true;
            return result;
        }

        LOG_INFO(CYLF_DEF, "\nMQTT connection failed with error code 0x%0X.\n",
               (int)result);

        vTaskDelay(MQTT_CONN_RETRY_INTERVAL_MS);
    }

    return result;
}

/*******************************************************************************
 *                          FUNCTION DEFINITIONS
 *****************************************************************************/

/******************************************************************************
 * Function Name: terminate_tasks
 ******************************************************************************
 * Summary:
 *  Cleanup section: Delete subscriber and publisher tasks and perform
 *
 * Parameters:
 *  void
 *
 *
 * Return:
 *  void
 *
 ******************************************************************************/
void terminate_tasks(void)
{
    LOG_INFO(CYLF_DEF, "\nTerminating Publisher and Subscriber tasks...\n");
    if (NULL != subscriber_task_handle )
    {
        vTaskDelete(subscriber_task_handle);
    }
    if (NULL != publisher_task_handle )
    {
        vTaskDelete(publisher_task_handle);
    }
    cleanup_mqtt();
    LOG_INFO(CYLF_DEF, "\nCleanup Done\nTerminating the MQTT task...\n\n");
    vTaskDelete(NULL);
}

/******************************************************************************
 * Function Name: mqtt_client_task
 ******************************************************************************
 * Summary:
 *  Task for handling initialization & connection of Wi-Fi and the MQTT client.
 *  The task also creates and manages the subscriber and publisher tasks upon 
 *  successful MQTT connection. The task also handles the WiFi and MQTT 
 *  connections by initiating reconnection on the event of disconnections.
 *
 * Parameters:
 *  void *pvParameters : Task parameter defined during task creation (unused)
 *
 * Return:
 *  void
 *
 ******************************************************************************/
void mqtt_client_task(void *pvParameters)
{
    /* Structures that store the data to be sent/received to/from various
     * message queues.
     */
    subscriber_data_t subscriber_q_data;
    cy_rslt_t result = CY_RSLT_SUCCESS;

    /* To avoid compiler warnings */
    (void) pvParameters;

    /* Mutex to guard uplink response */
    uplink_mutex = xSemaphoreCreateMutex();
    if (uplink_mutex == NULL)
    {
        LOG_ERROR(CYLF_DEF, "Uplink I2C mutex creation error.\n");
    }

    /* Create a message queue to communicate with other tasks and callbacks. */
    mqtt_task_q = xQueueCreate(MQTT_TASK_QUEUE_LENGTH, sizeof(mqtt_task_cmd_t));

    /* Set the appropriate bit in the status_flag to denote successful 
     * WCM initialization.
     */
    status_flag |= FLAG_WCM_INITIALIZED;
    LOG_INFO(CYLF_DEF, "\nWi-Fi Connection Manager initialized.\n");

    /* Create the subscriber task and cleanup if the operation fails. */
    if (pdPASS == xTaskCreate(subscriber_task, "Subscriber task", SUBSCRIBER_TASK_STACK_SIZE,
                              NULL, SUBSCRIBER_TASK_PRIORITY, &subscriber_task_handle))
    {
        /* Wait for the subscribe operation to complete. */
        vTaskDelay(pdMS_TO_TICKS(TASK_CREATION_DELAY_MS));

        /* Create the publisher task and cleanup if the operation fails. */
        if (pdPASS != xTaskCreate(publisher_task, "Publisher task", PUBLISHER_TASK_STACK_SIZE,
                                  NULL, PUBLISHER_TASK_PRIORITY, &publisher_task_handle))
        {
            ERROR_CHECK(1);
        }
    }

    while (true)
    {
        /* Wait for results of MQTT operations from other tasks and callbacks. */
        if (pdTRUE == xQueueReceive(mqtt_task_q, &mqtt_status, portMAX_DELAY))
        {
            /* In this code example, the disconnection from the MQTT Broker or
             * the Wi-Fi network is handled by the case 'HANDLE_DISCONNECTION'.
             *
             * The publish and subscribe failures (`HANDLE_MQTT_PUBLISH_FAILURE`
             * and `HANDLE_MQTT_SUBSCRIBE_FAILURE`) does not initiate
             * reconnection in this example, but they can be handled as per the
             * application requirement in the following swich cases.
             */
            switch(mqtt_status)
            {
                case HANDLE_DISCONNECTION:
                {
                    /* Although the connection with the MQTT Broker is lost,
                     * call the MQTT disconnect API for cleanup of threads and
                     * other resources before reconnection.
                     */
                    cy_mqtt_disconnect(mqtt_connection);

                    LOG_INFO(CYLF_DEF, "\nInitiating MQTT Reconnection... at %d mqtt status\n", mqtt_status);
                    if (CY_RSLT_SUCCESS == mqtt_connect())
                    {
                        CHECK_MQTT_CONNECTION_NEED_TO_CANCEL;

                        vTaskDelay(1000);

                        /* Initiate MQTT subscribe post the reconnection. */
                        subscriber_q_data.cmd = SUBSCRIBE_TO_TOPIC;
                        xQueueSend(subscriber_task_q, &subscriber_q_data, portMAX_DELAY);
                    }
                    else
                    {
                        handle_connectivity_state(STATE_CLOUD_DISCONNECT);
                    }

                    break;
                }

                /**
                 * Once WIFI connected try to connected with mqtt server
                 */
            case HANDLE_CONNECT:
            {
                if (!(status_flag & FLAG_MQTT_CONNECTION_SUCCESS))
                {
                    result = mqtt_connect();
                    if(CY_RSLT_SUCCESS == result)
                    {
                        CHECK_MQTT_CONNECTION_NEED_TO_CANCEL;

                        ble_disconnect();

                        /* Initiate MQTT subscribe post the reconnection. */
                        subscriber_q_data.cmd = SUBSCRIBE_TO_TOPIC;
                        xQueueSend(subscriber_task_q, &subscriber_q_data, portMAX_DELAY);

                        vTaskDelay(1000);
                    }
                    else
                    {
                        /* Don't update UI if the cloud cancel operation
                         * is performed */
                        if(is_cloud_connection_cancel != true)
                        {
                            update_conn_state(DEV_ST_CLOUD_DISCONNECTED);
                            vTaskDelay(2000);

                            /* Check if BLE is connected. If connected
                             * update UI to BLE connected. */
                            if(conn_id != false)
                            {
                                update_conn_state(DEV_ST_BLE_CONNECTED);
                            }
                        }

                        LOG_INFO(CYLF_DEF, "\nMQTT connect failed with error 0x%0X\n\n",
                               (int)result);
                        update_conn_state(DEV_ST_CLOUD_DISCONNECTED);

                        /* Let the app know connect failed instead of leaving it waiting. */
                        update_comm_interface();
                        send_response_numeric(DEVICE_STATUS, OPERATION_READ, 0);
                    }

                }
                else
                {
                    update_conn_state(DEV_ST_CLOUD_CONNECTED);
                }
                break;

                }
                case HANDLE_DEPROVISIONING:
                {
                    /* Delay added: MApp not receiving deprovisioning response. */
                    vTaskDelay(1000);
                    
                    if(cy_wcm_is_connected_to_ap())
                    {
                        uint32_t wait_to_publish = 0;

                        /* Initiate MQTT subscribe post the reconnection. */
                        subscriber_q_data.cmd = UNSUBSCRIBE_FROM_TOPIC;
                        xQueueSend(subscriber_task_q, &subscriber_q_data, portMAX_DELAY);

                        /**
                         * Wait for publish before disconnect
                         */
                        LOG_INFO(CYLF_DEF, "Available space in publish queue is %lu\n",uxQueueSpacesAvailable(publisher_task_q));
                        while(PUBLISHER_TASK_QUEUE_LENGTH != uxQueueSpacesAvailable(publisher_task_q)
                                && (wait_to_publish < (MQTT_PUBLISH_MAX_WAIT_TIME_IN_MS/100)) )
                        {
                            wait_to_publish++;
                            vTaskDelay(100);
                        }

                        vTaskDelay(500);

                        /* Although the connection with the MQTT Broker is lost,
                         * call the MQTT disconnect API for cleanup of threads and
                         * other resources before reconnection.
                         */
                        cleanup_mqtt();
                    }

                    LOG_INFO(CYLF_DEF, "Wi-Fi Disconnect at %s mqtt status: HANDLE_DEPROVISIONING.\n", __FUNCTION__);
                    xTaskNotify(wifi_task_handle, NOTIF_DISCONNECT | NOTIF_ERASE_DATA,
                                eSetValueWithOverwrite);

                    ble_disconnect();

                    /* Erase persisted BLE bond keys (LTK/IRK) from NVM so the
                     * device fully forgets the pairing on factory reset. This
                     * runs in both connected and disconnected states; on the
                     * next reconnection the peer must perform a fresh secure
                     * pairing. */
                    if (CY_RSLT_SUCCESS != app_bt_delete_bond_info())
                    {
                        LOG_ERROR(CYLF_DEF, "Failed to erase BLE bond keys from NVM.\n");
                    }
                    else
                    {
                        LOG_INFO(CYLF_DEF, "BLE bond keys erased from NVM.\n");
                    }

                    /* Clearing this flag as we are removing the Cloud connection */
                    is_cloud_connection_cancel = false;
                    break;
                }

                case HANDLE_MANUAL_DISCONNECTION:
                {
                    if((cy_wcm_is_connected_to_ap()) && (status_flag & FLAG_MQTT_CONNECTION_SUCCESS))
                    {
                        uint32_t wait_to_publish = 0;
                        /* Initiate MQTT subscribe post the reconnection. */
                        subscriber_q_data.cmd = UNSUBSCRIBE_FROM_TOPIC;
                        xQueueSend(subscriber_task_q, &subscriber_q_data, portMAX_DELAY);

                        /**
                         * Wait for publish before disconnect
                         */
                        LOG_INFO(CYLF_DEF, "Available space in publish queue is %lu\n",uxQueueSpacesAvailable(publisher_task_q));
                        while(PUBLISHER_TASK_QUEUE_LENGTH != uxQueueSpacesAvailable(publisher_task_q)
                                && (wait_to_publish < (MQTT_PUBLISH_MAX_WAIT_TIME_IN_MS/100)) )
                        {
                            wait_to_publish++;
                            vTaskDelay(100);
                        }

                        vTaskDelay(500);

                        /* Although the connection with the MQTT Broker is lost,
                         * call the MQTT disconnect API for cleanup of threads and
                         * other resources before reconnection.
                         */
                        mqtt_disconnect();

                        LOG_INFO(CYLF_DEF, "\nMQTT Disconnected!\n");


                        LOG_INFO(CYLF_DEF, "Wi-Fi Disconnect at %s mqtt status: HANDLE_MANUAL_DISCONNECTION 1.\n", __FUNCTION__);
                        xTaskNotify(wifi_task_handle, NOTIF_DISCONNECT,
                                    eSetValueWithOverwrite);

                        if(is_cloud_connection_cancel)
                        {
                            /* Check if BLE connected or not, and initiate
                             * actions accordingly. */
                            if (conn_id != false)
                            {
                                LOG_INFO(CYLF_DEF, "%s BLE already connected.\n", __FUNCTION__);
                                handle_connectivity_state(STATE_BLE_CONNECTED);
                                update_conn_state(DEV_ST_BLE_CONNECTED);
                            }
                            else
                            {
                                LOG_INFO(CYLF_DEF, "%s BLE start ADV.\n", __FUNCTION__);
                                handle_connectivity_state(STATE_BLE_ADV);
                            }
                            is_cloud_connection_cancel = false;
                        }
                    }
                    else
                    {
                        LOG_INFO(CYLF_DEF, "Wi-Fi Disconnect at %s mqtt status: HANDLE_MANUAL_DISCONNECTION 2.\n", __FUNCTION__);
                        xTaskNotify(wifi_task_handle, NOTIF_DISCONNECT, eSetValueWithOverwrite);
                    }
                    break;
                }
                case HANDLE_MQTT_SUBSCRIBE_FAILURE:
                {
                    cy_mqtt_disconnect(mqtt_connection);

                    vTaskDelay(1000);

                    mqtt_task_cmd_t mqtt_task_cmd = HANDLE_CONNECT;
                    xQueueSend(mqtt_task_q, &mqtt_task_cmd, portMAX_DELAY);
                    break;
                }
                default:
                    break;
            }
        }
        else
        {
            vTaskDelay(500);
        }
    }
}

/*******************************************************************************
* Function Name: get_mqtt_status
********************************************************************************
* Summary:
*  Returns the current MQTT status flags.
*
* Parameters:
*  void
*
* Return:
*  uint32_t - Current MQTT status flags.
*
*******************************************************************************/
uint32_t get_mqtt_status(void)
{
    return status_flag;
}

/*******************************************************************************
* Function Name: mqtt_disconnect
********************************************************************************
* Summary:
*  Disconnects from the MQTT broker.
*
* Parameters:
*  void
*
* Return:
*  void
*
*******************************************************************************/
void mqtt_disconnect(void)
{
    cy_rslt_t result = CY_RSLT_TYPE_ERROR;

    if(status_flag & FLAG_MQTT_CONNECTION_SUCCESS)
    {
        result = cy_mqtt_disconnect(mqtt_connection);
        if (CY_RSLT_SUCCESS == result)
        {
            LOG_INFO(CYLF_DEF, "Disconnected from the MQTT Broker...\n");
        }
        else
        {
            LOG_INFO(CYLF_DEF, "MQTT disconnect API failed unexpectedly.\n");
        }

        if(HANDLE_MANUAL_DISCONNECTION != mqtt_status && START_OTA_REMOVE_MQTT_INSTANCE != mqtt_status)
        {
            update_conn_state(DEV_ST_CLOUD_DISCONNECTED);
        }

        status_flag &= ~(FLAG_MQTT_CONNECTION_SUCCESS);
    }
}

#if GENERATE_UNIQUE_CLIENT_ID
/******************************************************************************
 * Function Name: mqtt_get_unique_client_identifier
 ******************************************************************************
 * Summary:
 *  Function that generates unique client identifier for the MQTT client by
 *  appending a timestamp to a common prefix 'MQTT_CLIENT_IDENTIFIER'.
 *
 * Parameters:
 *  char *mqtt_client_identifier : Pointer to the string that stores the 
 *                                 generated unique identifier
 *
 * Return:
 *  cy_rslt_t : CY_RSLT_SUCCESS on successful generation of the client 
 *              identifier, else a non-zero value indicating failure.
 *
 ******************************************************************************/
static cy_rslt_t mqtt_get_unique_client_identifier(char *mqtt_client_identifier)
{
    cy_rslt_t status = CY_RSLT_SUCCESS;

    /* Check for errors from snprintf. */
    if (0 > snprintf(mqtt_client_identifier,
                     (MQTT_CLIENT_IDENTIFIER_MAX_LEN + 1),
                     MQTT_CLIENT_IDENTIFIER "%lu",
                     (long unsigned int)Clock_GetTimeMs()))
    {
        status = ~CY_RSLT_SUCCESS;
    }

    return status;
}
#endif /* GENERATE_UNIQUE_CLIENT_ID */

/* [] END OF FILE */
