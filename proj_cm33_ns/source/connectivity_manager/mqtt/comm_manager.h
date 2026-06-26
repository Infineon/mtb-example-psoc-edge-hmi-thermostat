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

/*******************************************************************************
* Function Name: set_fan_speed
********************************************************************************
* Summary:
* Sets the fan speed to the specified value.
*
* Parameters:
*  value - Desired fan speed (fan_speed_t)
*
* Return:
*  void
*******************************************************************************/
void set_fan_speed(fan_speed_t value);

/*******************************************************************************
* Function Name: set_device_mode
********************************************************************************
* Summary:
* Sets the operating mode of the device (e.g., cooling, heating).
*
* Parameters:
*  value - Desired mode (thermostat_mode_t)
*
* Return:
*  void
*******************************************************************************/
void set_device_mode(thermostat_mode_t value);

/*******************************************************************************
* Function Name: set_device_temp
********************************************************************************
* Summary:
* Sets the target temperature for the device.
*
* Parameters:
*  value - Desired temperature (uint8_t)
*
* Return:
*  void
*******************************************************************************/
void set_device_temp(uint8_t value);

/*******************************************************************************
* Function Name: set_device_brightness
********************************************************************************
* Summary:
* Sets the screen brightness level of the device.
*
* Parameters:
*  level - Desired brightness level (uint8_t)
*
* Return:
*  void
*******************************************************************************/
void set_device_brightness(uint8_t level);

/*******************************************************************************
* Function Name: set_device_audio_level
********************************************************************************
* Summary:
* Sets the audio output level of the device.
*
* Parameters:
*  level - Desired audio level (uint8_t)
*
* Return:
*  void
*******************************************************************************/
void set_device_audio_level(uint8_t level);

/*******************************************************************************
* Function Name: display_ble_pin
********************************************************************************
* Summary:
* Displays the BLE pairing PIN on the device UI.
*
* Parameters:
*  pin - BLE PIN code (uint32_t)
*
* Return:
*  void
*******************************************************************************/
void display_ble_pin(uint32_t pin);

/*******************************************************************************
* Function Name: update_conn_state
********************************************************************************
* Summary:
* Updates the device's connection state.
*
* Parameters:
*  state - Connection state value (uint32_t)
*
* Return:
*  void
*******************************************************************************/
void update_conn_state(uint32_t state);

/*******************************************************************************
* Function Name: update_current_screen
********************************************************************************
* Summary:
* Updates the current screen displayed on the UI.
*
* Parameters:
*  state - Screen identifier (screen_id_t)
*
* Return:
*  void
*******************************************************************************/
void update_current_screen(screen_id_t state);

/*******************************************************************************
* Function Name: response_uid_req
********************************************************************************
* Summary:
* Sends a response containing the UID to the UI.
*
* Parameters:
*  uid - Pointer to UID string (char*)
*
* Return:
*  void
*******************************************************************************/
void response_uid_req(char *uid);

/*******************************************************************************
* Function Name: set_device_audio
********************************************************************************
* Summary:
* Sets the device's audio level using an enum value.
*
* Parameters:
*  value - Desired audio level (audio_level_t)
*
* Return:
*  void
*******************************************************************************/
void set_device_audio(audio_level_t value);

/*******************************************************************************
* Function Name: set_device_temp_unit
********************************************************************************
* Summary:
* Sets the temperature unit (e.g., Celsius or Fahrenheit).
*
* Parameters:
*  value - Desired temperature unit (temp_unit_t)
*
* Return:
*  void
*******************************************************************************/
void set_device_temp_unit(temp_unit_t value);


/*******************************************************************************
* Function Name: update_presence_detection_status
********************************************************************************
* Summary:
* Updates the presence detection status.
*
* Parameters:
*  status - Presence status (presence_status_t)
*
* Return:
*  void
*******************************************************************************/
void update_presence_detection_status(presence_status_t status);

/*******************************************************************************
* Function Name: update_provision_state
********************************************************************************
* Summary:
* Updates the device's provisioning state.
*
* Parameters:
*  state - Provisioning state (bool)
*
* Return:
*  void
*******************************************************************************/
void update_provision_state(bool state);

/*******************************************************************************
* Function Name: set_datetime_ui
********************************************************************************
* Summary:
* Sets the current date and time on the device UI.
*
* Parameters:
*  timestamp - Pointer to DateTime structure
*
* Return:
*  void
*******************************************************************************/
void set_datetime_ui(DateTime *timestamp);

/*******************************************************************************
* Function Name: no_internet_connected_send_to_ui
********************************************************************************
* Summary:
* Notifies the UI that no internet connection is available.
*
* Parameters:
*  void
*
* Return:
*  void
*******************************************************************************/
void no_internet_connected_send_to_ui(void);

/*******************************************************************************
* Function Name: disable_touch
********************************************************************************
* Summary:
* Disables touch input on the device.
*
* Parameters:
*  void
*
* Return:
*  void
*******************************************************************************/
void disable_touch(void);

void set_weather_code_sync(const char *value);
void set_weather_sync(const char *value);
void set_location_sync(const char *value);
void set_hour_sync(const char *value);
void set_minute_sync(const char *value);
void set_second_sync(const char *value);
void set_date_sync(const char *value);
void set_month_sync(const char *value);
void set_vaar_sync(const char *value);
void set_year_sync(const char *value);
void signal_http_fetching_start(void);
void signal_http_location_fetching(void);
void signal_http_weather_fetching(void);
void signal_http_datetime_fetching(void);

#ifdef __cplusplus
}
#endif

#endif /* COMM_MANAGER_H_ */
