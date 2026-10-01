/******************************************************************************
* File Name: wifi_task.h
*
* Description: This is the header file for wifi_task.c. It contains macros,
* enums and structures used by the functions in wifi_task.c. It also contains
* function prototypes and externs of global variables that can be used by
* other files
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

#ifndef __WIFI_TASK_H__
#define __WIFI_TASK_H__


#ifdef __cplusplus
extern "C" {
#endif
#include <FreeRTOS.h>
#include <task.h>
#include "cybsp.h"
#include "stdio.h"
#include "cy_wcm.h"
#include "cycfg_gatt_db.h"
#include "wiced_bt_stack.h"
#include "app_bt_utils.h"
#include "cycfg_gap.h"
#include "wiced_memory.h"
#include "retarget_io_init.h"
#include "mqtt_task.h"
#include "wireless_manager.h"
#include "comm_manager.h"
#include "wifi_utils.h"

/******************************************************************************
 * Constants
 ******************************************************************************/
/* Task parameters for WiFi tasks */
#define WIFI_TASK_STACK_SIZE              RTOS_STACK_SIZE_IN_BYTE((1024U * 3U))
#define WIFI_TASK_PRIORITY                (2U)

/* Macros defining the packet type */
#define DATA_PACKET_TYPE_SSID             (1U)
#define DATA_PACKET_TYPE_PASSWORD         (2U)

/* Macros defining packet type for scan data */
#define SCAN_PACKET_TYPE_SSID             (1U)
#define SCAN_PACKET_TYPE_SECURITY         (2U)
#define SCAN_PACKET_TYPE_RSSI             (3U)

/* Macros defining the commands for WiFi control point characteristic */
#define WIFI_CONTROL_DISCONNECT           (0U)
#define WIFI_CONTROL_CONNECT              (1U)
#define WIFI_CONTROL_SCAN                 (2U)
#define COMM_SWITCH_TO_WIFI               (3U)

#define APP_RRAM_NVM_MAIN_NS_START        0x22000000

#define RRAM_NVM_WIFI_CRED_OFFSET         (0x0003E000)
/* Task notification value to indicate whether to use
 * WiFi credentials from NVM or from GATT DB
 */
enum wifi_task_notifications
{
    NOTIF_SCAN               = 0x0001,
    NOTIF_CONNECT            = 0x0002,
    NOTIF_DISCONNECT         = 0x0004,
    NOTIF_ERASE_DATA         = 0x0008,
    NOTIF_GATT_NOTIFICATION  = 0x0010
};

/******************************************************************************
 * Structures
 ******************************************************************************/
/* Structure to store WiFi details that goes into NVM */
typedef __PACKED_STRUCT
{
    uint8_t wifi_ssid[CY_WCM_MAX_SSID_LEN + 1];
    uint8_t ssid_len;
    uint8_t wifi_password[CY_WCM_MAX_PASSPHRASE_LEN + 1];
    uint8_t password_len;
}wifi_details_t;

/******************************************************************************
 * Extern Variables
 ******************************************************************************/
extern wifi_details_t wifi_details;
extern wifi_details_t wifi_cred;

extern cy_wcm_connect_params_t wifi_conn_param;

/* Task Handle for WiFi Task */
extern TaskHandle_t wifi_task_handle;

extern uint8_t wifi_mac[6];

/******************************************************************************
 * Function Prototypes
 ******************************************************************************/
cy_rslt_t wcm_init(void);
cy_rslt_t wifi_connect(void);
void wifi_task(void * arg);

#ifdef __cplusplus
}
#endif

#endif      /* __WIFI_TASK_H__ */


/* [] END OF FILE */
