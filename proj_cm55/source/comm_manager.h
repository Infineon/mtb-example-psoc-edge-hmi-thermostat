/*******************************************************************************
 * File Name:   comm_manager.h
 *
 * Description:  Public interface for updating thermostat events from
 *               CM55 core to CM33 core using IPC.
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

#ifndef COMM_MANAGER_H_
#define COMM_MANAGER_H_


#ifdef __cplusplus
extern "C" {
#endif
/*******************************************************************************
 *                                INCLUDES
 *******************************************************************************/
#include "app_common.h"


/*******************************************************************************
 *                                MACROS
 *******************************************************************************/

/*******************************************************************************
 *                                CONSTANTS
 *******************************************************************************/

/*******************************************************************************
 *                                DATA TYPES
 *******************************************************************************/

/*******************************************************************************
 *                                GLOBAL VARIABLES
 *******************************************************************************/


/*******************************************************************************
 *                                FUNCTION PROTOTYPES
 *******************************************************************************/
void update_fan_speed_ipc(fan_speed_t value);
void update_device_mode_ipc(thermostat_mode_t value);
void update_current_temp_ipc(int temperature);
void update_brightness_ipc(uint8_t level);
void update_target_temp_ipc(int temperature);
void update_remainig_time_ipc(uint32_t time_s);
void update_temperature_data_ipc(device_state_t *data);
void update_system_unit_ipc(temp_unit_t unit);
void update_audio_level_ipc(uint16_t level);
void update_ble_adv_ipc(device_connection_state_t state);
void update_wifi_cred_ipc(const char *ssid, const char *passwd);
void update_switch_wifi_ipc(void);
void request_uid_ipc(void);
void request_wifi_delete_ipc(void);
void update_connection_retries_ipc(connection_retries_t state);
void update_connection_state_ipc(device_connection_state_t state);
void send_device_config(const device_state_t *config);
void send_humidity_to_m33(uint32_t hum);
void send_switch_to_ble_cmd(void);

#ifdef __cplusplus
}
#endif

#endif /* COMM_MANAGER_H_ */

/* [] END OF FILE */
