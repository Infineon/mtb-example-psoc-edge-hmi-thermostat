/******************************************************************************
* File Name:   subscriber_task.c
*
* Description: This file contains the task that initializes the user LED GPIO,
*              subscribes to the topic 'MQTT_SUB_TOPIC', and actuates the user LED
*              based on the notifications received from the MQTT subscriber
*              callback.
*
* Related Document: See README.md
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
#include "cybsp.h"
#include "string.h"
#include "FreeRTOS.h"

/* Task header files */
#include "subscriber_task.h"
#include "mqtt_task.h"
#include "wireless_manager.h"

/* Configuration file for MQTT client */
#include "mqtt_client_config.h"

/* Middleware libraries */
#include "cy_mqtt_api.h"
#include "cy_retarget_io.h"

#include <cJSON.h>
#include "mqtt/mqtt_command_handler.h"
#include "publisher_task.h"
/*******************************************************************************
 *                                MACROS
 *****************************************************************************/
/* Maximum number of retries for MQTT subscribe operation */
#define MAX_SUBSCRIBE_RETRIES                   (5U)

/* Time interval in milliseconds between MQTT subscribe retries. */
#define MQTT_SUBSCRIBE_RETRY_INTERVAL_MS        (1000U)

/* The number of MQTT topics to be subscribed to. */
#define SUBSCRIPTION_COUNT                      (1U)

/* Queue length of a message queue that is used to communicate with the 
 * subscriber task.
 */
#define SUBSCRIBER_TASK_QUEUE_LENGTH            (10U)
/*******************************************************************************
 *                              GLOBAL VARIABLES
 *****************************************************************************/
/* Task handle for this task. */
TaskHandle_t subscriber_task_handle;

/* Handle of the queue holding the commands for the subscriber task */
QueueHandle_t subscriber_task_q;

/* Variable to denote the current state of the user LED that is also used by 
 * the publisher task.
 */
uint32_t current_device_state = DEVICE_OFF_STATE;

/* Configure the subscription information structure. */
cy_mqtt_subscribe_info_t subscribe_info =
{
    .qos = (cy_mqtt_qos_t) MQTT_MESSAGES_QOS,
    .topic = mqtt_topics[0],
    .topic_len = MQTT_TOPIC_SIZE
};

/*****************************************************************************
 * Static Function Prototype
 *****************************************************************************/
/******************************************************************************
 * Function Name: subscribe_to_topic
 ******************************************************************************
 * Summary:
 *  Function that subscribes to the MQTT topic specified by the macro
 *  'MQTT_SUB_TOPIC'. This operation is retried a maximum of
 *  'MAX_SUBSCRIBE_RETRIES' times with interval of
 *  'MQTT_SUBSCRIBE_RETRY_INTERVAL_MS' milliseconds.
 *
 * Parameters:
 *  void
 *
 * Return:
 *  void
 *
 ******************************************************************************/
static void subscribe_to_topic(void);

/******************************************************************************
 * Function Name: unsubscribe_from_topic
 ******************************************************************************
 * Summary:
 *  Function that unsubscribes from the topic specified by the macro
 *  'MQTT_SUB_TOPIC'.
 *
 * Parameters:
 *  void
 *
 * Return:
 *  void
 *
 ******************************************************************************/
static void unsubscribe_from_topic(void);

/******************************************************************************
* Function definition
*******************************************************************************/
/*******************************************************************************
* Function Name: subscribe_to_topic
********************************************************************************
* Summary:
*  Subscribes the MQTT client to the configured topic.
*
* Parameters:
*  void
*
* Return:
*  void
*
*******************************************************************************/
static void subscribe_to_topic(void)
{
    /* Status variable */
    cy_rslt_t result = CY_RSLT_SUCCESS;

    /* Command to the MQTT client task */
    mqtt_task_cmd_t mqtt_task_cmd;

    /* Subscribe with the configured parameters. */
    for (uint32_t retry_count = 0; retry_count < MAX_SUBSCRIBE_RETRIES; retry_count++)
    {
        result = cy_mqtt_subscribe(mqtt_connection, &subscribe_info, SUBSCRIPTION_COUNT);
        if (result == CY_RSLT_SUCCESS)
        {
            LOG_INFO(CYLF_DEF, "MQTT client subscribed to the topic '%.*s' successfully.\n",
                    subscribe_info.topic_len, subscribe_info.topic);
            handle_connectivity_state(STATE_CLOUD_CONNECT);
            device_provisioned = true;
            break;
        }

        vTaskDelay(pdMS_TO_TICKS(MQTT_SUBSCRIBE_RETRY_INTERVAL_MS));
    }

    if (CY_RSLT_SUCCESS != result)
    {
        LOG_ERROR(CYLF_DEF, "MQTT Subscribe failed with error 0x%0X after %d retries...\n\n",
               (int)result, MAX_SUBSCRIBE_RETRIES);

        /* Notify the MQTT client task about the subscription failure */
        mqtt_task_cmd = HANDLE_MQTT_SUBSCRIBE_FAILURE;
        xQueueSend(mqtt_task_q, &mqtt_task_cmd, portMAX_DELAY);
    }
}
/*******************************************************************************
* Function Name: unsubscribe_from_topic
********************************************************************************
* Summary:
*  Unsubscribes the MQTT client from the configured topic.
*
* Parameters:
*  void
*
* Return:
*  void
*
*******************************************************************************/
static void unsubscribe_from_topic(void)
{
    cy_rslt_t result = cy_mqtt_unsubscribe(mqtt_connection,
                                           (cy_mqtt_unsubscribe_info_t *) &subscribe_info,
                                           SUBSCRIPTION_COUNT);

    if (CY_RSLT_SUCCESS != result)
    {
        LOG_ERROR(CYLF_DEF, "MQTT Unsubscribe operation failed with error 0x%0X!\n", (int)result);
    }
}
/******************************************************************************
 * Function Name: subscriber_task
 ******************************************************************************
 * Summary:
 *  Task that sets up the user LED GPIO, subscribes to the specified MQTT topic,
 *  and controls the user LED based on the received commands over the message 
 *  queue. The task can also unsubscribe from the topic based on the commands
 *  via the message queue.
 *
 * Parameters:
 *  void *pvParameters : Task parameter defined during task creation (unused)
 *
 * Return:
 *  void
 *
 ******************************************************************************/
void subscriber_task(void *pvParameters)
{
    subscriber_data_t subscriber_q_data;

    /* To avoid compiler warnings */
    (void) pvParameters;

    /* Create a message queue to communicate with other tasks and callbacks. */
    subscriber_task_q = xQueueCreate(SUBSCRIBER_TASK_QUEUE_LENGTH, sizeof(subscriber_data_t));

    while (true)
    {
        /* Wait for commands from other tasks and callbacks. */
        if (pdTRUE == xQueueReceive(subscriber_task_q, &subscriber_q_data, portMAX_DELAY))
        {
            switch(subscriber_q_data.cmd)
            {
                case SUBSCRIBE_TO_TOPIC:
                {
                    subscribe_to_topic();
                    break;
                }

                case UNSUBSCRIBE_FROM_TOPIC:
                {
                    unsubscribe_from_topic();
                    break;
                }

                case UPDATE_DEVICE_STATE:
                {
                    /* Do Nothing */
                    break;
                }
            }
        }
    }
}

/******************************************************************************
 * Function Name: mqtt_subscription_callback
 ******************************************************************************
 * Summary:
 *  Callback to handle incoming MQTT messages. This callback prints the 
 *  contents of the incoming message and informs the subscriber task, via a 
 *  message queue, to turn on / turn off the device based on the received 
 *  message.
 *
 * Parameters:
 *  cy_mqtt_publish_info_t *received_msg_info : Information structure of the 
 *                                              received MQTT message
 *
 * Return:
 *  void
 *
 ******************************************************************************/
void mqtt_subscription_callback(cy_mqtt_publish_info_t *received_msg_info)
{
    parse_received_command(received_msg_info->payload, received_msg_info->payload_len);
}

/*******************************************************************************
* Function Name: setuid
********************************************************************************
* Summary:
*  Sets the unique device identifier for MQTT topics.
*
* Parameters:
*  uidx - First byte of the UID.
*  uidy - Second byte of the UID.
*  uidz - Third byte of the UID.
*
* Return:
*  void
*
*******************************************************************************/
void setuid(uint8_t uidx, uint8_t uidy, uint8_t uidz)
{
    snprintf(mqtt_topics[0], MQTT_TOPIC_SIZE+1, "THERM/%X%X%X/CMD", uidx,uidy,uidz);
    snprintf(mqtt_topics[1], MQTT_TOPIC_SIZE+1, "THERM/%X%X%X/RSP", uidx,uidy,uidz);
}
/* [] END OF FILE */
