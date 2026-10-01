/*******************************************************************************
 * File Name:   comm_manager.h
 *
 * Description: Public interface for the IPC communication manager.
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
 *****************************************************************************/
#include "app_common.h"

void set_fan_speed(fan_speed_t value);

void set_device_mode(thermostat_mode_t value);

void set_device_temp(uint8_t value);

void set_device_brightness(uint8_t level);

void set_device_audio_level(uint8_t level);

void display_ble_pin(uint32_t pin);

void hide_connectivity_popup_ipc(void);

void update_conn_state(uint32_t state);

void update_current_screen(screen_id_t state);

void response_uid_req(char *uid);

void set_device_audio(audio_level_t value);

void set_device_temp_unit(temp_unit_t value);

void update_presence_detection_status(presence_status_t status);

void update_provision_state(bool state);

void set_datetime_ui(DateTime *timestamp);

void no_internet_connected_send_to_ui(void);

void disable_touch(void);

void set_weather_code_sync(const char *value);
void set_weather_sync(const char *value);
void set_location_sync(const char *value);
void signal_http_fetching_start(void);
void signal_http_fetch_failed(void);

void comm_manager_cache_init(void);

#ifdef __cplusplus
}
#endif

#endif /* COMM_MANAGER_H_ */
