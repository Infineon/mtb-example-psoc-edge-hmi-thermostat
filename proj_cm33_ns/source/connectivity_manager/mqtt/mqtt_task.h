/******************************************************************************
* File Name:   mqtt_task.h
*
* Description: This file is the public interface of mqtt_task.c
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

#ifndef MQTT_TASK_H_
#define MQTT_TASK_H_


#ifdef __cplusplus
extern "C" {
#endif
#include "FreeRTOS.h"
#include "queue.h"
#include "cy_mqtt_api.h"
#include "semphr.h"


/*******************************************************************************
 *                                MACROS
 *****************************************************************************/
/* Task parameters for MQTT Client Task. */
#define MQTT_TASK_NAME                             ("MQTT Client")
#define MQTT_CLIENT_TASK_PRIORITY                  (2U)
#define MQTT_CLIENT_TASK_STACK_SIZE                RTOS_STACK_SIZE_IN_BYTE((1024U * 8U))

#define MQTT_TOPIC_SIZE                     (16U)

#define NUMBERS_OF_TOPIC                    (2U)
/*******************************************************************************
 *                              GLOBAL VARIABLES
 *****************************************************************************/
/* Commands for the MQTT Client Task. */
typedef enum
{
    HANDLE_MQTT_SUBSCRIBE_FAILURE,
    HANDLE_MQTT_PUBLISH_FAILURE,
    HANDLE_DISCONNECTION,
    HANDLE_CONNECT,
    HANDLE_DEPROVISIONING,
    HANDLE_MANUAL_DISCONNECTION,
    START_OTA_REMOVE_MQTT_INSTANCE
} mqtt_task_cmd_t;

typedef enum
{
    FLAG_WCM_INITIALIZED            = (1lu << 0),
    FLAG_WIFI_CONNECTED             = (1lu << 1),
    FLAG_LIBS_INITIALIZED           = (1lu << 2),
    FLAG_BUFFER_INITIALIZED         = (1lu << 3),
    FLAG_MQTT_INSTANCE_CREATED      = (1lu << 4),
    FLAG_MQTT_CONNECTION_SUCCESS    = (1lu << 5),
    FLAG_MQTT_MSG_RECEIVED          = (1lu << 6)
} cleanup_flags_t;

typedef char mqtttopic_t[MQTT_TOPIC_SIZE + 1];

/*******************************************************************************
 * Extern variables
 ******************************************************************************/
extern cy_mqtt_t mqtt_connection;
extern QueueHandle_t mqtt_task_q;
extern SemaphoreHandle_t uplink_mutex;
extern mqtttopic_t mqtt_topics[NUMBERS_OF_TOPIC];

/*******************************************************************************
 *                          FUNCTION PROTOTYPES
 *****************************************************************************/
void mqtt_client_task(void *pvParameters);

void cleanup_mqtt(void);

uint32_t get_mqtt_status(void);

void mqtt_disconnect(void);

#ifdef __cplusplus
}
#endif

#endif /* MQTT_TASK_H_ */

/* [] END OF FILE */
