/*******************************************************************************
 * File Name:  comm_manager.c
 *
 * Description:
 *
 * This file implements the functions to send thermostat data over IPC
 * from CM55 core to CM33 core.
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
 ******************************************************************************/
#include "comm_manager.h"
#include "ipc_communication.h"
#include <stdint.h>

/*******************************************************************************
 *                              CONSTANTS
 ******************************************************************************/

/*******************************************************************************
 *                             GLOBAL VARIABLES
 ******************************************************************************/

/*******************************************************************************
 *                             STATIC VARIABLES
 ******************************************************************************/
CY_SECTION_SHAREDMEM static ipc_msg_t cm55_msg_data = 
{
    .client_id = CM33_IPC_PIPE_CLIENT_ID,
    .intr_mask = CY_IPC_CYPIPE_INTR_MASK_EP2,
    .data = 0,
    .data_len = 0,
    .device_config = {0}
};

/****************************************************************************
 *                              FUNCTION DECLARATIONS
 ***************************************************************************/

/*******************************************************************************
 *                              FUNCTION DEFINITIONS
 ******************************************************************************/

/*******************************************************************************
* Function Name: cm55_ipc_send_int
********************************************************************************
* Summary:
*  Populates and sends an int-payload IPC command. Holds the lock across
*  populate+send so callers don't repeat the lock/unlock boilerplate around
*  cm55_msg_data.
*
* Parameters:
*  cmd  - IPC command ID.
*  data - Int payload value.
*
* Return:
*  void
*
*******************************************************************************/
static void cm55_ipc_send_int(uint8_t cmd, int data)
{
    if (!cm55_ipc_lock())
    {
        printf("cm55_ipc_send_int: buffer busy, dropping cmd %u\n", cmd);
        return;
    }
    cm55_msg_data.cmd = cmd;
    cm55_msg_data.data = data;
    cm55_send_msg_cm33(&cm55_msg_data);
    cm55_ipc_unlock();
}

/*******************************************************************************
* Function Name: update_fan_speed_ipc
********************************************************************************
* Summary:
*  Sends an IPC message to update the fan speed on CM33.
*
* Parameters:
*  value - Fan speed value to set.
*
* Return:
*  void
*
*******************************************************************************/
void update_fan_speed_ipc(fan_speed_t value)
{
    cm55_ipc_send_int(IPC_CMD_SET_FAN_SPEED, value);
}

/*******************************************************************************
* Function Name: update_device_mode_ipc
********************************************************************************
* Summary:
*  Sends an IPC message to update the thermostat mode on CM33.
*
* Parameters:
*  value - Thermostat mode to set.
*
* Return:
*  void
*
*******************************************************************************/
void update_device_mode_ipc(thermostat_mode_t value)
{
    cm55_ipc_send_int(IPC_CMD_SET_THERMOSTAT_MODE, value);
}

/*******************************************************************************
* Function Name: update_current_temp_ipc
********************************************************************************
* Summary:
*  Sends an IPC message to update the current temperature on CM33.
*
* Parameters:
*  temperature - Current temperature value.
*
* Return:
*  void
*
*******************************************************************************/
void update_current_temp_ipc(int temperature)
{
    cm55_ipc_send_int(IPC_CMD_SET_CURRENT_TEMP, temperature);
}

/*******************************************************************************
* Function Name: update_brightness_ipc
********************************************************************************
* Summary:
*  Sends an IPC message to update the display brightness on CM33.
*
* Parameters:
*  level - Brightness level to set.
*
* Return:
*  void
*
*******************************************************************************/
void update_brightness_ipc(uint8_t level)
{
    cm55_ipc_send_int(IPC_CMD_SET_DISPLAY_BRIGHTNESS, level);
}

/*******************************************************************************
* Function Name: update_temperature_data_ipc
********************************************************************************
* Summary:
*  Sends temperature-related device state data via IPC to CM33.
*
* Parameters:
*  data - Pointer to device state structure containing temperature data.
*
* Return:
*  void
*
*******************************************************************************/
void update_temperature_data_ipc(device_state_t *data)
{
    if (!cm55_ipc_lock())
    {
        printf("update_temperature_data_ipc: buffer busy, dropping send\n");
        return;
    }
    cm55_msg_data.cmd = IPC_CMD_SET_TEMPERATURE_DATA;

    memset(&(cm55_msg_data.device_config), 0, sizeof(cm55_msg_data.device_config));
    memcpy(&(cm55_msg_data.device_config), data, sizeof(cm55_msg_data.device_config));

    cm55_send_msg_cm33(&cm55_msg_data);
    cm55_ipc_unlock();
}

/*******************************************************************************
* Function Name: update_audio_level_ipc
********************************************************************************
* Summary:
*  Sends an IPC message to update the audio level on CM33.
*
* Parameters:
*  level - Audio level value.
*
* Return:
*  void
*
*******************************************************************************/
void update_audio_level_ipc(uint16_t level)
{
    cm55_ipc_send_int(IPC_CMD_SET_AUDIO_LEVEL, level);
}

/*******************************************************************************
* Function Name: update_ble_adv_ipc
********************************************************************************
* Summary:
*  Sends an IPC message to update the BLE advertising/connection state.
*
* Parameters:
*  state - Device connection state for BLE.
*
* Return:
*  void
*
*******************************************************************************/
void update_ble_adv_ipc(device_connection_state_t state)
{
    cm55_ipc_send_int(IPC_CMD_UPDATE_CONN_STATE, state);
}

/*******************************************************************************
* Function Name: update_wifi_cred_ipc
********************************************************************************
* Summary:
*  Sends an IPC message with Wi-Fi credentials (SSID and password) to CM33.
*
* Parameters:
*  ssid   - Pointer to the SSID string.
*  passwd - Pointer to the password string.
*
* Return:
*  void
*
*******************************************************************************/
void update_wifi_cred_ipc(const char *ssid, const char *passwd)
{
    if (!cm55_ipc_lock())
    {
        printf("update_wifi_cred_ipc: buffer busy, dropping send\n");
        return;
    }
    cm55_msg_data.cmd = IPC_CMD_UPDATE_CONN_STATE;
    cm55_msg_data.data = DEV_ST_WIFI_CONNECTING;

    memset(&(cm55_msg_data.wifi_info), 0, sizeof(cm55_msg_data.wifi_info));
    strncpy(cm55_msg_data.wifi_info.ssid, ssid, sizeof(cm55_msg_data.wifi_info.ssid) - 1);
    cm55_msg_data.wifi_info.ssid[sizeof(cm55_msg_data.wifi_info.ssid) - 1] = '\0';

    strncpy(cm55_msg_data.wifi_info.password, passwd, sizeof(cm55_msg_data.wifi_info.password) - 1);
    cm55_msg_data.wifi_info.password[sizeof(cm55_msg_data.wifi_info.password) - 1] = '\0';

    cm55_send_msg_cm33(&cm55_msg_data);
    cm55_ipc_unlock();
}

/*******************************************************************************
* Function Name: update_switch_wifi_ipc
********************************************************************************
* Summary:
*  Sends an IPC command to switch the device to Wi-Fi mode.
*
* Parameters:
*  void
*
* Return:
*  void
*
*******************************************************************************/
void update_switch_wifi_ipc(void)
{
    cm55_ipc_send_int(IPC_CMD_UPDATE_CONN_STATE, DEV_ST_SWITCH_WIFI);
}

/*******************************************************************************
* Function Name: request_uid_ipc
********************************************************************************
* Summary:
*  Sends an IPC request to obtain the unique device ID from CM33.
*
* Parameters:
*  void
*
* Return:
*  void
*
*******************************************************************************/
void request_uid_ipc(void)
{
    cm55_ipc_send_int(IPC_CMD_GET_UID, 0);
}

/*******************************************************************************
* Function Name: request_wifi_delete_ipc
********************************************************************************
* Summary:
*  Sends an IPC request to delete saved Wi-Fi credentials on CM33.
*
* Parameters:
*  void
*
* Return:
*  void
*
*******************************************************************************/
void request_wifi_delete_ipc(void)
{
    cm55_ipc_send_int(IPC_CMD_RESET_WIFI_SSID_PASS, 0);
}

/*******************************************************************************
* Function Name: update_connection_retries_ipc
********************************************************************************
* Summary:
*  Sends an IPC message to update the connection retry policy.
*
* Parameters:
*  state - Connection retry state (limited or infinite).
*
* Return:
*  void
*
*******************************************************************************/
void update_connection_retries_ipc(connection_retries_t state)
{
    cm55_ipc_send_int(IPC_CMD_UPDATE_CONN_RETRIES, state);
}

/*******************************************************************************
* Function Name: update_connection_state_ipc
********************************************************************************
* Summary:
*  Sends an IPC message to update the device connection state on CM33.
*
* Parameters:
*  state - The new device connection state to set.
*
* Return:
*  void
*
*******************************************************************************/
void update_connection_state_ipc(device_connection_state_t state)
{
    cm55_ipc_send_int(IPC_CMD_UPDATE_CONN_STATE, state);
}

/*******************************************************************************
* Function Name: send_device_config
********************************************************************************
* Summary:
*  Sends the complete device configuration via IPC to CM33.
*
* Parameters:
*  config - Pointer to the device configuration structure.
*
* Return:
*  void
*
*******************************************************************************/
void send_device_config(const device_state_t *config)
{
    if (!cm55_ipc_lock())
    {
        printf("send_device_config: buffer busy, dropping send\n");
        return;
    }
    cm55_msg_data.cmd = IPC_CMD_DEVICE_CONFIG;
    cm55_msg_data.data = 0;
    cm55_msg_data.device_config = *config;

    cm55_send_msg_cm33(&cm55_msg_data);
    cm55_ipc_unlock();
}

/*******************************************************************************
* Function Name: send_humidity_to_m33
********************************************************************************
* Summary:
*  Sends the current humidity value via IPC to CM33.
*
* Parameters:
*  hum - Humidity value to send.
*
* Return:
*  void
*
*******************************************************************************/
void send_humidity_to_m33(uint32_t hum)
{
    cm55_ipc_send_int(IPC_CMD_GET_CURRENT_HUMIDITY, hum);
}

/*******************************************************************************
* Function Name: send_switch_to_ble_cmd
********************************************************************************
* Summary:
*  Sends an IPC command to switch the device to BLE mode.
*
* Parameters:
*  void
*
* Return:
*  void
*
*******************************************************************************/
void send_switch_to_ble_cmd(void)
{
    cm55_ipc_send_int(IPC_CMD_SWITCH_TO_BLE, 0);
}

/* [] END OF FILE */
