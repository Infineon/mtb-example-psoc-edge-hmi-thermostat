/******************************************************************************
* File Name:   publisher_task.c
*
* Description: This file contains the task that sets up the user button GPIO 
*              for the publisher and publishes MQTT messages on the topic
*              'MQTT_PUB_TOPIC' to control a device that is actuated by the
*              subscriber task. The file also contains the ISR that notifies
*              the publisher task about the new device state to be published.
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

#include "cy_log.h"
#include "cybsp.h"
#include "FreeRTOS.h"

/* Task header files */
#include "publisher_task.h"
#include "mqtt_task.h"
#include "subscriber_task.h"

/* Configuration file for MQTT client */
#include "mqtt_client_config.h"

/* Middleware libraries */
#include "cy_mqtt_api.h"
#include "retarget_io_init.h"
#include "app_common.h"

/*******************************************************************************
 *                                MACROS
 *****************************************************************************/
/* The maximum number of times each PUBLISH in this example will be retried. */
#define PUBLISH_RETRY_LIMIT             (3U)

/* A PUBLISH message is retried if no response is received within this 
 * time (in milliseconds).
 */
#define PUBLISH_RETRY_MS                (100U)

#define MAX_PUB_RETRY_THRESHOLD         (50U)

/*******************************************************************************
 *                          FUNCTION PROTOTYPES
 *****************************************************************************/

/*******************************************************************************
 *                              GLOBAL VARIABLES
 *****************************************************************************/
/* FreeRTOS task handle for this task. */
TaskHandle_t publisher_task_handle;

/* Handle of the queue holding the commands for the publisher task */
QueueHandle_t publisher_task_q;

/* Structure to store publish message information. */
cy_mqtt_publish_info_t publish_info =
{
    .qos = (cy_mqtt_qos_t) MQTT_MESSAGES_QOS,
    .topic = mqtt_topics[1],
    .topic_len = MQTT_TOPIC_SIZE,
    .retain = false,
    .dup = false
};

cy_mqtt_publish_info_t publish_info_FW_version =
{
    .qos = (cy_mqtt_qos_t) MQTT_MESSAGES_QOS,
    .topic = "OTA-Update/KIT_PSE84_HMI/OTA-Notify",
    .topic_len = 39,
    .retain = false,
    .dup = false
};
/******************************************************************************
 * Function Name: publisher_init
 ******************************************************************************
 * Summary:
 *  Function that initializes and sets-up the user button GPIO pin along with  
 *  its interrupt.
 * 
 * Parameters:
 *  void
 *
 * Return:
 *  void
 *
 ******************************************************************************/
static void publisher_init(void)
{
    /* Initialize before publish */
}

/******************************************************************************
 * Function Name: publisher_deinit
 ******************************************************************************
 * Summary:
 *  Disables the user button interrupt
 *
 * Parameters:
 *  void
 *
 * Return:
 *  void
 *
 ******************************************************************************/
static void publisher_deinit(void)
{
    /* Do deinit for publisher */
}

/******************************************************************************
 * Function Name: publisher_task
 ******************************************************************************
 * Summary:
 *  Task that sets up the user button GPIO for the publisher and publishes
 *  MQTT messages to the broker. The user button init and deinit operations,
 *  and the MQTT publish operation is performed based on commands sent by other
 *  tasks and callbacks over a message queue.
 *
 * Parameters:
 *  void *pvParameters : Task parameter defined during task creation (unused)
 *
 * Return:
 *  void
 *
 ******************************************************************************/
void publisher_task(void *pvParameters)
{
    uint8_t publish_retry = 0;
    publisher_data_t publisher_q_data;

    /* Command to the MQTT client task */
    mqtt_task_cmd_t mqtt_task_cmd;

    /* To avoid compiler warnings */
    CY_UNUSED_PARAMETER(pvParameters);

    /* Create a message queue to communicate with other tasks and callbacks. */
    publisher_task_q = xQueueCreate(PUBLISHER_TASK_QUEUE_LENGTH, sizeof(publisher_data_t));

    while (true)
    {
        /* Wait for commands from other tasks and callbacks. */
        if (pdTRUE == xQueueReceive(publisher_task_q, &publisher_q_data, portMAX_DELAY))
        {
            if(!(get_mqtt_status() & FLAG_MQTT_CONNECTION_SUCCESS))
            {
                LOG_INFO(CYLF_DEF, "Publisher: Unable to publish because the cloud is not connected.\r\n");
                continue;
            }

            switch(publisher_q_data.cmd)
            {
                case PUBLISHER_INIT:
                {
                    /* Initialize and set-up the user button GPIO. */
                    publisher_init();
                    break;
                }

                case PUBLISHER_DEINIT:
                {
                    /* Deinit the user button GPIO and corresponding interrupt. */
                    publisher_deinit();
                    break;
                }

                case PUBLISH_MQTT_MSG:
                {
                    /* Status variable */
                    cy_rslt_t result = !CY_RSLT_SUCCESS;

                    /* Publish the data received over the message queue. */
                    publish_info.payload = publisher_q_data.data;
                    publish_info.payload_len = strlen(publish_info.payload);
                    
                    if(publish_info.payload_len < 50)
                    {                    
                        LOG_INFO(CYLF_DEF, "Publisher: Publishing '%s' on the topic '%s'\n",
                        (char *) publish_info.payload, publish_info.topic);
                    }

                    for(int retry = 0; ((retry < PUBLISH_RETRY_LIMIT) && (result != CY_RSLT_SUCCESS)); retry++)
                    {
                        result = cy_mqtt_publish(mqtt_connection, &publish_info);
                        vTaskDelay(PUBLISH_RETRY_MS);
                    }

                    if (result != CY_RSLT_SUCCESS)
                    {
                        LOG_ERROR(CYLF_DEF, "Publisher: MQTT Publish failed with error 0x%0X.\n", (int)result);

                        if(MAX_PUB_RETRY_THRESHOLD <= publish_retry++)
                        {
                            /* Communicate the publish failure with the the MQTT
                             * client task.
                             */
                            mqtt_task_cmd = HANDLE_DISCONNECTION;
                            xQueueSend(mqtt_task_q, &mqtt_task_cmd, portMAX_DELAY);
                        }
                    }
                    else
                    {
                        publish_retry = 0;
                    }

                    free(publisher_q_data.data);
                    break;
                }

                case PUBLISH_MQTT_MSG_FOR_FW_VERSION:
                {
                    /* Status variable */
                    cy_rslt_t result = !CY_RSLT_SUCCESS;

                    /* Publish the data received over the message queue. */
                    publish_info_FW_version.payload = publisher_q_data.data;
                    publish_info_FW_version.payload_len = strlen(publish_info_FW_version.payload);

                    LOG_INFO(CYLF_DEF, "Publisher: Publishing '%s' on the topic '%s'\n",
                           (char *) publish_info_FW_version.payload, publish_info_FW_version.topic);

                    for(int retry = 0; ((retry < PUBLISH_RETRY_LIMIT) && (result != CY_RSLT_SUCCESS)); retry++)
                    {
                        result = cy_mqtt_publish(mqtt_connection, &publish_info_FW_version);
                        vTaskDelay(PUBLISH_RETRY_MS);
                    }

                    if (result != CY_RSLT_SUCCESS)
                    {
                        LOG_ERROR(CYLF_DEF, "Publisher: MQTT Publish failed with error 0x%0X.\n", (int)result);
                    }

                    break;
                }
            }
        }
    }
}

/*******************************************************************************
* Function Name: publish_msg_over_mqtt
********************************************************************************
* Summary:
*  Publishes a message over MQTT by queueing data to the publisher task.
*
* Parameters:
*  data - Pointer to the message data string.
*
* Return:
*  void
*
*******************************************************************************/
void publish_msg_over_mqtt(char *data)
{
    publisher_data_t publisher_q_data;

    publisher_q_data.cmd = PUBLISH_MQTT_MSG;
    publisher_q_data.data = data;
    xQueueSend(publisher_task_q, &publisher_q_data, portMAX_DELAY);
}

/*******************************************************************************
* Function Name: publish_msg_for_FW_version
********************************************************************************
* Summary:
*  Publishes firmware version message over MQTT.
*
* Parameters:
*  data - Pointer to the firmware version data string.
*
* Return:
*  void
*
*******************************************************************************/
void publish_msg_for_FW_version(char *data)
{
    publisher_data_t publisher_q_data;

    publisher_q_data.cmd = PUBLISH_MQTT_MSG_FOR_FW_VERSION;
    publisher_q_data.data = data;
    xQueueSend(publisher_task_q, &publisher_q_data, portMAX_DELAY);
}
/* [] END OF FILE */
