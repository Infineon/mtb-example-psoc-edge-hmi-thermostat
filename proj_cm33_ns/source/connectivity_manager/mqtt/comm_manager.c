/*******************************************************************************
 * File Name:   comm_manager.c
 *
 * Description: Implementation of IPC communication manager for sending
 *              thermostat data from CM33 core to CM55 core.
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

/*******************************************************************************
 *                                INCLUDES
 *****************************************************************************/
#include "comm_manager.h"
#include "ipc_communication.h"
#include "wifi_task.h"
#include "wireless_manager.h"

CY_SECTION_SHAREDMEM static ipc_msg_t cm33_msg_data = 
{
    .client_id = CM55_IPC_PIPE_CLIENT_ID,
    .intr_mask = CY_IPC_CYPIPE_INTR_MASK_EP2,
    .data = 0,
    .data_len = 0,
    .device_config = {0}
};

/*******************************************************************************
* Function Name: set_fan_speed
********************************************************************************
* Summary:
*  Sends IPC command to set fan speed.
*
* Parameters:
*  value - Fan speed value.
*
* Return:
*  void
*
*******************************************************************************/
void set_fan_speed(fan_speed_t value)
{
    cm33_msg_data.cmd = IPC_CMD_SET_FAN_SPEED;
    cm33_msg_data.data = value;
    cm33_msg_data.data_len = 1;
    cm33_send_msg_cm55(&cm33_msg_data);
}


/*******************************************************************************
* Function Name: set_device_mode
********************************************************************************
* Summary:
*  Sends IPC command to set thermostat mode.
*
* Parameters:
*  value - Thermostat mode value.
*
* Return:
*  void
*
*******************************************************************************/
void set_device_mode(thermostat_mode_t value)
{
    cm33_msg_data.cmd = IPC_CMD_SET_THERMOSTAT_MODE;
    cm33_msg_data.data = value;
    cm33_msg_data.data_len = 1;
    cm33_send_msg_cm55(&cm33_msg_data);
}

/*******************************************************************************
* Function Name: set_device_temp
********************************************************************************
* Summary:
*  Sends IPC command to set target temperature.
*
* Parameters:
*  value - Target temperature value.
*
* Return:
*  void
*
*******************************************************************************/
void set_device_temp(uint8_t value)
{
    cm33_msg_data.cmd = IPC_CMD_SET_TARGET_TEMP;
    cm33_msg_data.data = value;
    cm33_msg_data.data_len = 1;
    cm33_send_msg_cm55(&cm33_msg_data);
}

/*******************************************************************************
* Function Name: set_device_brightness
********************************************************************************
* Summary:
*  Sends IPC command to set display brightness.
*
* Parameters:
*  level - Brightness level.
*
* Return:
*  void
*
*******************************************************************************/
void set_device_brightness(uint8_t level)
{
    cm33_msg_data.cmd = IPC_CMD_SET_DISPLAY_BRIGHTNESS;
    cm33_msg_data.data = level;
    cm33_msg_data.data_len = 1;
    cm33_send_msg_cm55(&cm33_msg_data);
}

/*******************************************************************************
* Function Name: set_device_audio_level
********************************************************************************
* Summary:
*  Sends IPC command to set audio level.
*
* Parameters:
*  level - Audio level.
*
* Return:
*  void
*
*******************************************************************************/
void set_device_audio_level(uint8_t level)
{
    cm33_msg_data.cmd = IPC_CMD_SET_AUDIO_LEVEL;
    cm33_msg_data.data = level;
    cm33_msg_data.data_len = 1;
    cm33_send_msg_cm55(&cm33_msg_data);
}

/*******************************************************************************
* Function Name: display_ble_pin
********************************************************************************
* Summary:
*  Sends IPC command to display BLE PIN.
*
* Parameters:
*  pin - BLE PIN value.
*
* Return:
*  void
*
*******************************************************************************/
void display_ble_pin(uint32_t pin)
{
    cm33_msg_data.cmd = IPC_CMD_CURRENT_EVENT;
    cm33_msg_data.data = pin;
    cm33_msg_data.data_len = 1;
    cm33_send_msg_cm55(&cm33_msg_data);
}

/*******************************************************************************
* Function Name: update_conn_state
********************************************************************************
* Summary:
*  Updates connection state via IPC.
*
* Parameters:
*  state - Connection state value.
*
* Return:
*  void
*
*******************************************************************************/
void update_conn_state(uint32_t state)
{
    /*If a Wi-Fi cancel operation is in progress, do not update the Wi-Fi-related UI*/
    if(is_wifi_operation_canceled())
    {
        if(DEV_ST_WIFI_STATE_MIN >= state || DEV_ST_WIFI_STATE_MAX <= state)
        {
            return;
        }
    }

    /*If a BLE is connected, do not update the Wi-Fi-related UI*/
    if(get_ble_connection_status())
    {
        state = DEV_ST_BLE_CONNECTED;
    }
    cm33_msg_data.cmd = IPC_CMD_UPDATE_CONN_STATE;
    cm33_msg_data.data = state;
    cm33_msg_data.data_len = 1;
    cm33_send_msg_cm55(&cm33_msg_data);
}


/*******************************************************************************
* Function Name: update_current_screen
********************************************************************************
* Summary:
*  Sends IPC command to update current screen.
*
* Parameters:
*  screen - Screen ID to display.
*
* Return:
*  void
*
*******************************************************************************/
void update_current_screen(screen_id_t screen)
{
    cm33_msg_data.cmd = IPC_CMD_UPDATE_CURRENT_SCREEN;
    cm33_msg_data.data = screen;
    cm33_msg_data.data_len = 1;
    cm33_send_msg_cm55(&cm33_msg_data);
}

/*******************************************************************************
* Function Name: response_uid_req
********************************************************************************
* Summary:
*  Sends UID response via IPC.
*
* Parameters:
*  uid - Pointer to UID string.
*
* Return:
*  void
*
*******************************************************************************/
void response_uid_req(char *uid)
{
    cm33_msg_data.cmd = IPC_CMD_SET_UID;
    memcpy(cm33_msg_data.unique_id, (char*)uid, 13);

    cm33_send_msg_cm55(&cm33_msg_data);
}

/*******************************************************************************
* Function Name: set_device_audio
********************************************************************************
* Summary:
*  Sends IPC command to set audio level.
*
* Parameters:
*  value - Audio level value.
*
* Return:
*  void
*
*******************************************************************************/
void set_device_audio(audio_level_t value)
{
    cm33_msg_data.cmd = IPC_CMD_SET_AUDIO_LEVEL;
    cm33_msg_data.data = value;

    cm33_send_msg_cm55(&cm33_msg_data);
}

/*******************************************************************************
* Function Name: set_device_temp_unit
********************************************************************************
* Summary:
*  Sends IPC command to set temperature unit.
*
* Parameters:
*  value - Temperature unit value.
*
* Return:
*  void
*
*******************************************************************************/
void set_device_temp_unit(temp_unit_t value)
{
    cm33_msg_data.cmd = IPC_CMD_SET_TEMP_UNIT;
    cm33_msg_data.data = value;

    cm33_send_msg_cm55(&cm33_msg_data);
}

/*******************************************************************************
* Function Name: update_presence_detection_status
********************************************************************************
* Summary:
*  Sends IPC command to update presence detection status.
*
* Parameters:
*  status - Presence detection status.
*
* Return:
*  void
*
*******************************************************************************/
void update_presence_detection_status(presence_status_t status)
{
    cm33_msg_data.cmd = IPC_CMD_UPDATE_PRESENCE_STATUS;
    cm33_msg_data.data = status;

    cm33_send_msg_cm55(&cm33_msg_data);
}

/*******************************************************************************
* Function Name: update_provision_state
********************************************************************************
* Summary:
*  Sends IPC command to update provision state.
*
* Parameters:
*  state - Provision state.
*
* Return:
*  void
*
*******************************************************************************/
void update_provision_state(bool state)
{
    cm33_msg_data.cmd = IPC_CMD_UPDATE_PROVISION_STATE;
    cm33_msg_data.data = state;
    cm33_msg_data.data_len = 1;
    cm33_send_msg_cm55(&cm33_msg_data);
}

/*******************************************************************************
* Function Name: set_datetime_ui
********************************************************************************
* Summary:
*  Sends date/time data via IPC.
*
* Parameters:
*  timestamp - Pointer to DateTime structure.
*
* Return:
*  void
*
*******************************************************************************/
void set_datetime_ui(DateTime *timestamp)
{
    cm33_msg_data.cmd = IPC_CMD_SET_DATE_TIME;
    memcpy(&cm33_msg_data.datetime, (DateTime*)timestamp, sizeof(DateTime));

    cm33_send_msg_cm55(&cm33_msg_data);
}

/*******************************************************************************
* Function Name: no_internet_connected_send_to_ui
********************************************************************************
* Summary:
*  Sends no internet connection notification via IPC.
*
* Parameters:
*  void
*
* Return:
*  void
*
*******************************************************************************/
void no_internet_connected_send_to_ui(void)
{
    cm33_msg_data.cmd = IPC_CMD_NO_INTERNET_NOTIFY;

    cm33_send_msg_cm55(&cm33_msg_data);
}


/*******************************************************************************
* Function Name: disable_touch
********************************************************************************
* Summary:
*  Sends IPC command to disable touch input.
*
* Parameters:
*  void
*
* Return:
*  void
*
*******************************************************************************/
void disable_touch(void)
{
    cm33_msg_data.cmd = IPC_CMD_DISABLE_TOUCH;

    cm33_send_msg_cm55(&cm33_msg_data);
}

/*******************************************************************************
* Function Name: set_weather_sync
********************************************************************************
* Summary:
*  Sends weather data synchronization via IPC.
*
* Parameters:
*  value - Weather data string.
*
* Return:
*  void
*
*******************************************************************************/
void set_weather_sync(const char *value)
{
    cm33_msg_data.cmd = IPC_CMD_WEATHER_SYNC;

    // Clear and copy the string safely
    memset(cm33_msg_data.char_value, 0, sizeof(cm33_msg_data.char_value));
    strncpy(cm33_msg_data.char_value, value, sizeof(cm33_msg_data.char_value) - 1);  // Null-terminate

    cm33_send_msg_cm55(&cm33_msg_data);
}

/*******************************************************************************
* Function Name: set_location_sync
********************************************************************************
* Summary:
*  Sends location data synchronization via IPC.
*
* Parameters:
*  value - Location data string.
*
* Return:
*  void
*
*******************************************************************************/
void set_location_sync(const char *value)
{
    cm33_msg_data.cmd = IPC_CMD_LOCATION_SYNC;

    // Clear and copy the string safely
    memset(cm33_msg_data.char_value, 0, sizeof(cm33_msg_data.char_value));
    strncpy(cm33_msg_data.char_value, value, sizeof(cm33_msg_data.char_value) - 1);  // Null-terminate

    cm33_send_msg_cm55(&cm33_msg_data);
}

/*******************************************************************************
* Function Name: set_hour_sync
********************************************************************************
* Summary:
*  Sends hour data synchronization via IPC.
*
* Parameters:
*  value - Hour data string.
*
* Return:
*  void
*
*******************************************************************************/
void set_hour_sync(const char *value)
{
    cm33_msg_data.cmd = IPC_CMD_HOUR_SYNC;

    // Clear and copy the string safely
    memset(cm33_msg_data.char_value, 0, sizeof(cm33_msg_data.char_value));
    strncpy(cm33_msg_data.char_value, value, sizeof(cm33_msg_data.char_value) - 1);  // Null-terminate

    cm33_send_msg_cm55(&cm33_msg_data);
}

/*******************************************************************************
* Function Name: set_minute_sync
********************************************************************************
* Summary:
*  Sends minute data synchronization via IPC.
*
* Parameters:
*  value - Minute data string.
*
* Return:
*  void
*
*******************************************************************************/
void set_minute_sync(const char *value)
{
    cm33_msg_data.cmd = IPC_CMD_MINUTE_SYNC;

    // Clear and copy the string safely
    memset(cm33_msg_data.char_value, 0, sizeof(cm33_msg_data.char_value));
    strncpy(cm33_msg_data.char_value, value, sizeof(cm33_msg_data.char_value) - 1);  // Null-terminate

    cm33_send_msg_cm55(&cm33_msg_data);
}

/*******************************************************************************
* Function Name: set_second_sync
********************************************************************************
* Summary:
*  Sends second data synchronization via IPC.
*
* Parameters:
*  value - Second data string.
*
* Return:
*  void
*
*******************************************************************************/
void set_second_sync(const char *value)
{
    cm33_msg_data.cmd = IPC_CMD_SECOND_SYNC;

    // Clear and copy the string safely
    memset(cm33_msg_data.char_value, 0, sizeof(cm33_msg_data.char_value));
    strncpy(cm33_msg_data.char_value, value, sizeof(cm33_msg_data.char_value) - 1);  // Null-terminate

    cm33_send_msg_cm55(&cm33_msg_data);
}

/*******************************************************************************
* Function Name: set_date_sync
********************************************************************************
* Summary:
*  Sends date data synchronization via IPC.
*
* Parameters:
*  value - Date data string.
*
* Return:
*  void
*
*******************************************************************************/
void set_date_sync(const char *value)
{
    cm33_msg_data.cmd = IPC_CMD_DATE_SYNC;

    // Clear and copy the string safely
    memset(cm33_msg_data.char_value, 0, sizeof(cm33_msg_data.char_value));
    strncpy(cm33_msg_data.char_value, value, sizeof(cm33_msg_data.char_value) - 1);  // Null-terminate

    cm33_send_msg_cm55(&cm33_msg_data);
}

/*******************************************************************************
* Function Name: set_month_sync
********************************************************************************
* Summary:
*  Sends month data synchronization via IPC.
*
* Parameters:
*  value - Month data string.
*
* Return:
*  void
*
*******************************************************************************/
void set_month_sync(const char *value)
{
    cm33_msg_data.cmd = IPC_CMD_MONTH_SYNC;

    // Clear and copy the string safely
    memset(cm33_msg_data.char_value, 0, sizeof(cm33_msg_data.char_value));
    strncpy(cm33_msg_data.char_value, value, sizeof(cm33_msg_data.char_value) - 1);  // Null-terminate

    cm33_send_msg_cm55(&cm33_msg_data);
}

/*******************************************************************************
* Function Name: set_vaar_sync
********************************************************************************
* Summary:
*  Sends vaar data synchronization via IPC.
*
* Parameters:
*  value - Vaar data string.
*
* Return:
*  void
*
*******************************************************************************/
void set_vaar_sync(const char *value)
{
    cm33_msg_data.cmd = IPC_CMD_VAAR_SYNC;

    // Clear and copy the string safely
    memset(cm33_msg_data.char_value, 0, sizeof(cm33_msg_data.char_value));
    strncpy(cm33_msg_data.char_value, value, sizeof(cm33_msg_data.char_value) - 1);  // Null-terminate

    cm33_send_msg_cm55(&cm33_msg_data);
}

/*******************************************************************************
* Function Name: set_year_sync
********************************************************************************
* Summary:
*  Sends year data synchronization via IPC.
*
* Parameters:
*  value - Year data string.
*
* Return:
*  void
*
*******************************************************************************/
void set_year_sync(const char *value)
{
    cm33_msg_data.cmd = IPC_CMD_YEAR_SYNC;

    // Clear and copy the string safely
    memset(cm33_msg_data.char_value, 0, sizeof(cm33_msg_data.char_value));
    strncpy(cm33_msg_data.char_value, value, sizeof(cm33_msg_data.char_value) - 1);  // Null-terminate

    cm33_send_msg_cm55(&cm33_msg_data);
}

/*******************************************************************************
* Function Name: set_weather_code_sync
********************************************************************************
* Summary:
*  Sends weather code synchronization via IPC.
*
* Parameters:
*  value - Weather code data string.
*
* Return:
*  void
*
*******************************************************************************/
void set_weather_code_sync(const char *value)
{
    cm33_msg_data.cmd = IPC_CMD_WEATHER_CODE_SYNC;

    // Clear and copy the string safely
    memset(cm33_msg_data.char_value, 0, sizeof(cm33_msg_data.char_value));
    strncpy(cm33_msg_data.char_value, value, sizeof(cm33_msg_data.char_value) - 1);  // Null-terminate

    cm33_send_msg_cm55(&cm33_msg_data);
}

/*******************************************************************************
* Function Name: signal_http_fetching_start
********************************************************************************
* Summary:
*  Sends IPC signal that HTTP fetching has started.
*
* Parameters:
*  void
*
* Return:
*  void
*
*******************************************************************************/
void signal_http_fetching_start(void)
{
    cm33_msg_data.cmd = IPC_CMD_HTTP_FETCHING_START;
    cm33_msg_data.data_len = 0;

    cm33_send_msg_cm55(&cm33_msg_data);
}

/*******************************************************************************
* Function Name: signal_http_location_fetching
********************************************************************************
* Summary:
*  Sends IPC signal for HTTP location fetching.
*
* Parameters:
*  void
*
* Return:
*  void
*
*******************************************************************************/
void signal_http_location_fetching(void)
{
    cm33_msg_data.cmd = IPC_CMD_HTTP_LOCATION_FETCHING;
    cm33_msg_data.data_len = 0;

    cm33_send_msg_cm55(&cm33_msg_data);
}

/*******************************************************************************
* Function Name: signal_http_weather_fetching
********************************************************************************
* Summary:
*  Sends IPC signal for HTTP weather fetching.
*
* Parameters:
*  void
*
* Return:
*  void
*
*******************************************************************************/
void signal_http_weather_fetching(void)
{
    cm33_msg_data.cmd = IPC_CMD_HTTP_WEATHER_FETCHING;
    cm33_msg_data.data_len = 0;

    cm33_send_msg_cm55(&cm33_msg_data);
}

/*******************************************************************************
* Function Name: signal_http_datetime_fetching
********************************************************************************
* Summary:
*  Sends IPC signal for HTTP datetime fetching.
*
* Parameters:
*  void
*
* Return:
*  void
*
*******************************************************************************/
void signal_http_datetime_fetching(void)
{
    cm33_msg_data.cmd = IPC_CMD_HTTP_DATETIME_FETCHING;
    cm33_msg_data.data_len = 0;

    cm33_send_msg_cm55(&cm33_msg_data);
}

