/******************************************************************************
* File Name:   subscriber_task.h
*
* Description: This file is the public interface of subscriber_task.c
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

#ifndef SUBSCRIBER_TASK_H_
#define SUBSCRIBER_TASK_H_


#ifdef __cplusplus
extern "C" {
#endif
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "cy_mqtt_api.h"

/*******************************************************************************
 *                                MACROS
 *****************************************************************************/
/* Task parameters for Subscriber Task. */
#define SUBSCRIBER_TASK_PRIORITY           (2U)
#define SUBSCRIBER_TASK_STACK_SIZE         (1024U * 2U)

/* 8-bit value denoting the device (LED) state. */
#define DEVICE_ON_STATE                    (0x01U)
#define DEVICE_OFF_STATE                   (0x00U)

/*******************************************************************************
 *                              GLOBAL VARIABLES
 *****************************************************************************/
/* Commands for the Subscriber Task. */
typedef enum
{
    SUBSCRIBE_TO_TOPIC,
    UNSUBSCRIBE_FROM_TOPIC,
    UPDATE_DEVICE_STATE
} subscriber_cmd_t;

/* Struct to be passed via the subscriber task queue */
typedef struct{
    subscriber_cmd_t cmd;
    uint8_t data;
} subscriber_data_t;

/*******************************************************************************
 *                          EXTERN VARIABLES
 *****************************************************************************/
extern TaskHandle_t subscriber_task_handle;
extern QueueHandle_t subscriber_task_q;
extern uint32_t current_device_state;

/*******************************************************************************
 *                          FUNCTION PROTOTYPES
 *****************************************************************************/
void subscriber_task(void *pvParameters);
void mqtt_subscription_callback(cy_mqtt_publish_info_t *received_msg_info);
void setuid(uint8_t uidx, uint8_t uidy, uint8_t uidz);

#ifdef __cplusplus
}
#endif

#endif /* SUBSCRIBER_TASK_H_ */

/* [] END OF FILE */
