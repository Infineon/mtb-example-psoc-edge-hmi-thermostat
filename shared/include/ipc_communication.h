/*******************************************************************************
* File Name        : ipc_communication.h
*
* Description      : This file contains the headers and structures
*                    used to setup the IPC Pipes between CM33 and CM55
*
* Related Document : See README.md
*
********************************************************************************
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

#ifndef SOURCE_IPC_COMMUNICATION_H
#define SOURCE_IPC_COMMUNICATION_H


#ifdef __cplusplus
extern "C" {
#endif
/*******************************************************************************
* Header Files
*******************************************************************************/
#include "cybsp.h"
#include "cy_pdl.h"
#include "cy_ipc_pipe.h"
#include "app_common.h"

/*******************************************************************************
* Macros
*******************************************************************************/
#define CY_IPC_MAX_ENDPOINTS            (5UL)
#define CY_IPC_CYPIPE_CLIENT_CNT        (16UL)

#define CY_IPC_CHAN_CYPIPE_EP1          (4UL)
#define CY_IPC_INTR_CYPIPE_EP1          (4UL)
#define CY_IPC_CHAN_CYPIPE_EP2          (15UL)
#define CY_IPC_INTR_CYPIPE_EP2          (5UL)

/* IPC Pipe Endpoint-1 config */
#define CY_IPC_CYPIPE_CHAN_MASK_EP1     CY_IPC_CH_MASK(CY_IPC_CHAN_CYPIPE_EP1)
#define CY_IPC_CYPIPE_INTR_MASK_EP1     CY_IPC_INTR_MASK(CY_IPC_INTR_CYPIPE_EP1)
#define CY_IPC_INTR_CYPIPE_PRIOR_EP1    (4UL)//(1UL)
#define CY_IPC_INTR_CYPIPE_MUX_EP1      (CY_IPC0_INTR_MUX(CY_IPC_INTR_CYPIPE_EP1))
#define CM33_IPC_PIPE_EP_ADDR           (1UL)
#define CM33_IPC_PIPE_CLIENT_ID         (3UL)

/* IPC Pipe Endpoint-2 config */
#define CY_IPC_CYPIPE_CHAN_MASK_EP2     CY_IPC_CH_MASK(CY_IPC_CHAN_CYPIPE_EP2)
#define CY_IPC_CYPIPE_INTR_MASK_EP2     CY_IPC_INTR_MASK(CY_IPC_INTR_CYPIPE_EP2)
#define CY_IPC_INTR_CYPIPE_PRIOR_EP2    (4UL)//(1UL)
#define CY_IPC_INTR_CYPIPE_MUX_EP2      (CY_IPC0_INTR_MUX(CY_IPC_INTR_CYPIPE_EP2))
#define CM55_IPC_PIPE_EP_ADDR           (2UL)
#define CM55_IPC_PIPE_CLIENT_ID         (5UL)

/* Combined Interrupt Mask */
#define CY_IPC_CYPIPE_INTR_MASK         ( CY_IPC_CYPIPE_CHAN_MASK_EP1 | CY_IPC_CYPIPE_CHAN_MASK_EP2)

/* Command messages sent from CM55 to CM33 */
#define IPC_CMD_INIT                    (0x81)
#define IPC_CMD_START                   (0x82)
#define IPC_CMD_STOP                    (0x83)
#define IPC_CMD_STATUS                  (0x41)

#define IPC_MUTEX_TIMEOUT               1000U

/*******************************************************************************
* Enumeration
*******************************************************************************/
/* IPC Message structure - CM55 -> CM33 direction.
 * Pointer to this structure will be shared through IPC Pipe.
 * device_config used to be a permanent field present in every message of this
 * type (even ones that never touch it, e.g. a plain conn-state scalar), which
 * wasted ~120 bytes per message. Folding it into the union is safe because on
 * THIS (CM55 -> CM33) path the union is only ever populated with device_config
 * OR wifi_info - each in its own send function in proj_cm55 comm_manager.c
 * (send_device_config / update_temperature_data_ipc set device_config;
 * update_wifi_cred_ipc sets wifi_info), each fully repopulating the buffer
 * under cm55_ipc_lock, so no single send writes two members. The CM33 receiver
 * (proj_cm33_ns app_ui_receiver.c) reads back exactly the member selected by
 * .cmd (device_config under IPC_CMD_DEVICE_CONFIG / IPC_CMD_SET_TEMPERATURE_DATA,
 * wifi_info under IPC_CMD_UPDATE_CONN_STATE+DEV_ST_WIFI_CONNECTING), so no
 * message is ever read through the wrong arm. char_value/datetime/unique_id are
 * NOT used on this path at all - they belong to the CM33 -> CM55 struct below,
 * which has no device_config, so a char_value send cannot alias device_config.
 * Full read/write audit attached to the MR. */
typedef struct
{
    uint8_t     client_id;
    uint16_t    intr_mask;
    uint8_t     cmd;
    uint8_t     data_len;
    int         data;

    union {
        wifi_credentials_t    wifi_info;
        ble_pairing_code_t    pairing_code;
        char unique_id[13];        /* Unique id created using MAC addr. */
        DateTime datetime;
        char fw_version[MAX_FW_VERSION_LEN];
        char char_value[32];       /* Used for single character values */
        device_state_t device_config;
    };

} ipc_msg_t;

/* IPC Message structure - CM33 -> CM55 direction only.
 * This direction never carries wifi/ble/device_config payloads (audited: only
 * unique_id, DateTime and short char_value strings cross this way), so it is
 * kept as its own, much smaller struct instead of sharing ipc_msg_t. Word 0
 * (client_id + intr_mask) is byte-for-byte identical in position to ipc_msg_t
 * above - required by the PDL pipe, which reads/writes that word directly. */
typedef struct
{
    uint8_t     client_id;
    uint16_t    intr_mask;
    uint8_t     cmd;
    uint8_t     data_len;
    int         data;

    union {
        char unique_id[13];        /* Unique id created using MAC addr. */
        DateTime datetime;
        char char_value[32];       /* Used for single character values */
    };

} ipc_msg_cm33_to_cm55_t;

/*******************************************************************************
* Function prototypes
*******************************************************************************/
void cm33_ipc_communication_setup(void);
void cm33_ipc_pipe_isr(void);
/* Must be called only while holding cm33_ipc_lock() (that returned true): on a
 * pipe-send error it gives cm33_ipc_send_done_sem back, which pairs with the
 * take done in cm33_ipc_lock(). Calling it unlocked leaves a spurious give and
 * corrupts the buffer-handshake count. */
int cm33_send_msg_cm55(ipc_msg_cm33_to_cm55_t *msg);
/* Hold across a shared cm33_msg_data field-populate + cm33_send_msg_cm55 call
 * so two CM33 tasks can never interleave writes into the single shared
 * message buffer. Recursive: safe to call even though cm33_send_msg_cm55
 * also takes this lock internally. Returns false if the buffer could not be
 * acquired before the timeout (CM55 still reading the previous message); the
 * caller must then NOT populate/send and must NOT call cm33_ipc_unlock(). */
bool cm33_ipc_lock(void);
void cm33_ipc_unlock(void);
void cm55_ipc_communication_setup(void);
void cm55_ipc_pipe_isr(void);
int cm55_send_msg_cm33(ipc_msg_t *msg);
bool cm55_ipc_lock(void);
void cm55_ipc_unlock(void);

#ifdef __cplusplus
}
#endif

#endif /* SOURCE_IPC_COMMUNICATION_H */
