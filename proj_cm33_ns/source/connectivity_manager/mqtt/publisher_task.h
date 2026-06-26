/******************************************************************************
* File Name:   publisher_task.h
*
* Description: This file is the public interface of publisher_task.c
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

#ifndef PUBLISHER_TASK_H_
#define PUBLISHER_TASK_H_


#ifdef __cplusplus
extern "C" {
#endif
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

/*******************************************************************************
 *                                MACROS
 *****************************************************************************/
/* Task parameters for Button Task. */
#define PUBLISHER_TASK_PRIORITY               (2U)
#define PUBLISHER_TASK_STACK_SIZE             (1024U *2U)

/* Queue length of a message queue that is used to communicate with the
 * publisher task.
 */
#define PUBLISHER_TASK_QUEUE_LENGTH     (30U)

/*******************************************************************************
 *                              GLOBAL VARIABLES
 *****************************************************************************/
/* Commands for the Publisher Task. */
typedef enum
{
    PUBLISHER_INIT,
    PUBLISHER_DEINIT,
    PUBLISH_MQTT_MSG,
    PUBLISH_MQTT_MSG_FOR_FW_VERSION
} publisher_cmd_t;

/* Struct to be passed via the publisher task queue */
typedef struct{
    publisher_cmd_t cmd;
    char *data;
} publisher_data_t;

/*******************************************************************************
 *                          EXTERN VARIABLES
 *****************************************************************************/
extern TaskHandle_t publisher_task_handle;
extern QueueHandle_t publisher_task_q;

/*******************************************************************************
 *                          FUNCTION PROTOTYPES
 *****************************************************************************/
/*******************************************************************************
* Function Name: publisher_task
********************************************************************************
* Summary:
* Initializes and runs the MQTT publisher task responsible for sending messages.
*
* Parameters:
*  pvParameters - Pointer to task parameters (void*)
*
* Return:
*  void
*******************************************************************************/
void publisher_task(void *pvParameters);

/*******************************************************************************
* Function Name: publish_msg_over_mqtt
********************************************************************************
* Summary:
* Publishes a generic message over MQTT.
*
* Parameters:
*  data - Pointer to the message string to be published (char*)
*
* Return:
*  void
*******************************************************************************/
void publish_msg_over_mqtt(char* data);

/*******************************************************************************
* Function Name: publish_msg_for_FW_version
********************************************************************************
* Summary:
* Publishes the get firmware version request over MQTT to get new firmware version.
*
* Parameters:
*  data - Pointer to the firmware version string (char*)
*
* Return:
*  void
*******************************************************************************/
void publish_msg_for_FW_version(char* data);

#ifdef __cplusplus
}
#endif

#endif /* PUBLISHER_TASK_H_ */

/* [] END OF FILE */
