/******************************************************************************
* File Name:   mqtt_command_handler.c
*
* Description:   Implements MQTT command parsing and creation.
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
#include <stdio.h>
#include <stdlib.h>
#include "mqtt_command_handler.h"
#include "cJSON.h"
#include "publisher_task.h"
#include "subscriber_task.h"
#include "comm_manager.h"
#include "mqtt_task.h"
#include "wireless_manager.h"

/*******************************************************************************
 *                                MACROS
 *****************************************************************************/
#define JSON_KEY_FOR_TYPEOFOPERATION            "type"
#define JSON_KEY_FOR_COMMAND                    "cmd_id"
#define JSON_KEY_FOR_VALUE                        "value"

#define SHIFT_LEFT(shift_amount) ((1) << (shift_amount))
#define IS_BIT_SET(value, bit) (((value) & (1U << (bit))) != 0)
/**
 * Global Declaration
 */
publisher_data_t publisher_q_data;

device_state_t device_status;

static connectivity_medium_t current_interface = CONNECTIVITY_NONE;

const unsigned int common_get_command = SHIFT_LEFT(DEVICE_STATUS) | SHIFT_LEFT(TARGETED_TEMP) |
                                    SHIFT_LEFT(GETDEVICE_CURRENT_HUMIDITY) | SHIFT_LEFT(GETDEVICE_CURRENT_CO2LEVEL) |
                                    SHIFT_LEFT(FIRMWARE_UPDATE_PROGRESS) | SHIFT_LEFT(FIRMWARE_UPDATE_STATUS);

typedef struct
{
    mqtt_commandId_e command;
    operation_type_e type;
    bool is_numeric;
    double value;
    char *buff;
}jsonpyload_t;
/**
 * Static Function Declarations
 */
/*******************************************************************************
* Function Name: json_parsenumeric
********************************************************************************
* Summary:
* Parses a numeric value from the specified JSON message using the given token key.
*
* Parameters:
*  message - JSON-formatted string to parse (const char*)
*  token   - Key name to search for (const char*)
*
* Return:
*  double - Parsed numeric value
*******************************************************************************/
static double json_parsenumeric(const char *message, const char *token);

/*******************************************************************************
* Function Name: json_parsestring
********************************************************************************
* Summary:
* Parses a JSON string to extract a string value associated with a given token.
*
* Parameters:
*  message - JSON-formatted string to parse (const char*)
*  token   - Key name to search for (const char*)
*
* Return:
*  char* - Dynamically allocated string containing the key's value if found, otherwise NULL
*******************************************************************************/
static char* json_parsestring(const char *message, const char *token);

/*******************************************************************************
* Function Name: handle_devicestatus_command
********************************************************************************
* Summary:
* Handles the DEVICE_STATUS command and responds with the current status of the device.
*
* Parameters:
*  void
*
* Return:
*  void
*******************************************************************************/
static void handle_devicestatus_command(void);

/*******************************************************************************
* Function Name: handle_datetime_command
********************************************************************************
* Summary:
* Handles the Set DateTime command and responds with the status.
*
* Parameters:
*  value - DateTime string value (char*)
*
* Return:
*  void
*******************************************************************************/
static void handle_datetime_command(char *value);

/*******************************************************************************
* Function Name: handle_mqtt_command_str
********************************************************************************
* Summary:
* Main dispatcher for incoming MQTT commands with string values; routes them to appropriate handlers.
*
* Parameters:
*  command - MQTT command ID (mqtt_commandId_e)
*  type    - Operation type (operation_type_e)
*  value   - Command value string (char*)
*
* Return:
*  void
*******************************************************************************/
static void handle_mqtt_command_str(mqtt_commandId_e command, operation_type_e type, char *value);

/*******************************************************************************
* Function Name: handle_devicemode_command
********************************************************************************
* Summary:
* Handles the DEVICE_MODE command to set or get the current device mode.
*
* Parameters:
*  type - Operation type (operation_type_e)
*  mode - Desired device mode (thermostat_mode_t)
*
* Return:
*  void
*******************************************************************************/
static void handle_devicemode_command(operation_type_e type, thermostat_mode_t mode);

/*******************************************************************************
* Function Name: handle_gettergettemp_command
********************************************************************************
* Summary:
* Handles the TARGETED_TEMP command to fetch the current target temperature.
*
* Parameters:
*  void
*
* Return:
*  void
*******************************************************************************/
static void handle_gettergettemp_command(void);

/*******************************************************************************
* Function Name: handle_settempupdown_command
********************************************************************************
* Summary:
* Handles the SETDEVICE_TEMP_UPDOWN command to adjust temperature up or down.
*
* Parameters:
*  updown - Temperature adjustment direction (temp_updown_e)
*
* Return:
*  void
*******************************************************************************/
static void handle_settempupdown_command(temp_updown_e updown);

/*******************************************************************************
* Function Name: handle_fanspeed_command
********************************************************************************
* Summary:
* Handles the FAN_SPEED command to set or get the current fan speed.
*
* Parameters:
*  type  - Operation type (operation_type_e)
*  speed - Desired fan speed (fan_speed_t)
*
* Return:
*  void
*******************************************************************************/
static void handle_fanspeed_command(operation_type_e type, fan_speed_t speed);

/*******************************************************************************
* Function Name: handle_getcurrentpara_command
********************************************************************************
* Summary:
* Handles commands to retrieve current environmental parameters such as humidity or CO₂.
*
* Parameters:
*  cmd - MQTT command ID (mqtt_commandId_e)
*
* Return:
*  void
*******************************************************************************/
static void handle_getcurrentpara_command(mqtt_commandId_e cmd);

/*******************************************************************************
* Function Name: handle_currenttemp_command
********************************************************************************
* Summary:
* Handles commands to retrieve or set the current temperature.
*
* Parameters:
*  type  - Operation type (operation_type_e)
*  value - Temperature value (uint32_t)
*
* Return:
*  void
*******************************************************************************/
static void handle_currenttemp_command(operation_type_e type, uint32_t value);

/*******************************************************************************
* Function Name: handle_brightness_command
********************************************************************************
* Summary:
* Handles the DEVICE_BRIGHTNESS command to update or retrieve the display brightness level.
*
* Parameters:
*  type       - Operation type (operation_type_e)
*  brightness - Brightness level (uint8_t)
*
* Return:
*  void
*******************************************************************************/
static void handle_brightness_command(operation_type_e type, uint8_t brightness);

/*******************************************************************************
* Function Name: handle_deviceaudio_command
********************************************************************************
* Summary:
* Handles the DEVICE_AUDIO command to set or retrieve the audio volume level.
*
* Parameters:
*  type  - Operation type (operation_type_e)
*  level - Audio level (uint8_t)
*
* Return:
*  void
*******************************************************************************/
static void handle_deviceaudio_command(operation_type_e type, uint8_t level);


/*******************************************************************************
* Function Name: handle_getdeviceconfig_command
********************************************************************************
* Summary:
* Processes the command to retrieve the current device configuration.
*
* Parameters:
*  void
*
* Return:
*  void
*******************************************************************************/
static void handle_getdeviceconfig_command(void);

/*******************************************************************************
* Function Name: handle_tempunit_command
********************************************************************************
* Summary:
* Processes the TEMP_UNIT command to set or get the temperature unit.
*
* Parameters:
*  type - Operation type (operation_type_e)
*  unit - Temperature unit (temp_unit_t)
*
* Return:
*  void
*******************************************************************************/
static void handle_tempunit_command(operation_type_e type, temp_unit_t unit);

/*******************************************************************************
* Function Name: handle_mqtt_command
********************************************************************************
* Summary:
* Main dispatcher for incoming MQTT commands with numeric values; routes them to appropriate handlers.
*
* Parameters:
*  command - MQTT command ID (mqtt_commandId_e)
*  type    - Operation type (operation_type_e)
*  value   - Command value (uint32_t)
*
* Return:
*  void
*******************************************************************************/
static void handle_mqtt_command(mqtt_commandId_e command, operation_type_e type, uint32_t value);

/*******************************************************************************
* Function Name: send_response_String
********************************************************************************
* Summary:
* Sends a response back over MQTT with the specified command, type, and string data.
*
* Parameters:
*  cmd         - MQTT command ID (mqtt_commandId_e)
*  type        - Operation type (operation_type_e)
*  string_data - Pointer to response string (char*)
*
* Return:
*  void
*******************************************************************************/
void send_response_String(mqtt_commandId_e cmd, operation_type_e type, char *string_data);

/*******************************************************************************
* Function Name: parse_datetime_string
********************************************************************************
* Summary:
* Parses a date and time string into a DateTime structure.
*
* Parameters:
*  datetime_str - Date and time string in "YYYY-MM-DD HH:MM:SS" format (const char*)
*  parsed_time  - Pointer to DateTime structure to store the result
*
* Return:
*  bool - true if parsing was successful, false otherwise
*******************************************************************************/
static bool parse_datetime_string(const char *datetime_str, DateTime *parsed_time);

/*******************************************************************************
* Function Name: json_create_response
********************************************************************************
* Summary:
* Creates a JSON-formatted response string for the given command, type, and value.
*
* Parameters:
*  pyload - JSON payload structure (jsonpyload_t)
*
* Return:
*  char* - Pointer to dynamically allocated JSON response string. Caller must free.
*******************************************************************************/
static char *json_create_response(jsonpyload_t pyload);

/**
 * Static Function Definitions
 */

/*******************************************************************************
* Function Name: json_parsenumeric
********************************************************************************
* Summary:
*  Parses a numeric value from a JSON message string.
*
* Parameters:
*  message - The JSON message string.
*  tokan - The JSON key to extract.
*
* Return:
*  double - The parsed numeric value, or -1 on failure.
*
*******************************************************************************/
static double json_parsenumeric(const char *message, const char *tokan)
{
    double number = -1;
    /* Example: Parse JSON payload from MQTT message */
    cJSON *json = cJSON_Parse(message);
    if (json) {
        number = cJSON_GetObjectItem(json, tokan)->valuedouble;
        LOG_INFO(CYLF_DEF, "Parsed value: %g for tokan %s\n", number, tokan);
        cJSON_Delete(json);
    } else {
        LOG_ERROR(CYLF_DEF, "Failed to parse incoming json message for tokan: %s.\n", tokan);
    }

    return number;
}

/*******************************************************************************
* Function Name: json_parsestring
********************************************************************************
* Summary:
*  Extracts a string value from a JSON message.
*
* Parameters:
*  message - The JSON message string.
*  token - The JSON key to extract.
*
* Return:
*  char* - Allocated string with the value, or NULL on failure.
*
*******************************************************************************/
static char* json_parsestring(const char *message, const char *token)
{
    char* result = NULL;
    cJSON *json = NULL;
    cJSON *item = NULL;

    /* Parse the JSON payload from the message string. */
    json = cJSON_Parse(message);
    if (json == NULL) {
        LOG_ERROR(CYLF_DEF, "Failed to parse incoming JSON message.\r\n");
        return result;
    }

    /* Get the specified item from the parsed JSON object. */
    item = cJSON_GetObjectItem(json, token);
    if (item == NULL) {
        LOG_INFO(CYLF_DEF, "Failed to find token '%s' in JSON message.\r\n", token);
        cJSON_Delete(json);
        return result;
    }

    /* Check if the found item is a string and retrieve its value. */
    if (cJSON_IsString(item)) {
        /* Explicitly allocate memory and copy the string. */
        const char *valuestring = item->valuestring;
        if (valuestring != NULL) {
            size_t len = strlen(valuestring);
            result = (char*)malloc(len + 1); /* +1 for the null terminator */
            if (result != NULL) {
                memcpy(result, valuestring, len + 1);
                LOG_INFO(CYLF_DEF, "Parsed value: '%s' for token '%s'\r\n", result, token);
            } else {
                LOG_ERROR(CYLF_DEF, "Memory allocation failed for string value.\r\n");
            }
        }
    } else {
        LOG_ERROR(CYLF_DEF, "Token '%s' is not a string. Returning NULL.\r\n", token);
    }

    /* Clean up the cJSON object to free memory. */
    cJSON_Delete(json);

    return result;
}

/*******************************************************************************
* Function Name: handle_devicestatus_command
********************************************************************************
* Summary:
*  Sends the current device status response.
*
* Parameters:
*  void
*
* Return:
*  void
*
*******************************************************************************/
static void handle_devicestatus_command(void)
{
    send_response_numeric(DEVICE_STATUS, OPERATION_READ, (uint32_t)1);
}

/*******************************************************************************
* Function Name: parse_datetime_string
********************************************************************************
* Summary:
*  Parses a date-time string into a DateTime struct.
*
* Parameters:
*  datetime_str - The date-time string to parse.
*  parsed_time - Pointer to DateTime struct for output.
*
* Return:
*  bool - true if parsing succeeded.
*
*******************************************************************************/
static bool parse_datetime_string(const char *datetime_str, DateTime *parsed_time)
{
    if (datetime_str == NULL || parsed_time == NULL) {
        LOG_ERROR(CYLF_DEF, "Error: Invalid input pointers.\r\n");
        return false;
    }

    /* Use sscanf to parse the string with the specified format. */
    /* The format string "%d-%d-%d %d:%d:%d" matches the input pattern exactly. */
    int result = sscanf(datetime_str, "%d-%d-%d %d:%d:%d",
                        &parsed_time->year,
                        &parsed_time->month,
                        &parsed_time->day,
                        &parsed_time->hour,
                        &parsed_time->minute,
                        &parsed_time->second);

    /* sscanf returns the number of successfully matched items. */
    /* We expect 6 items (year, month, day, hour, minute, second). */
    if (result == 6) {
        return true;
    } else {
        LOG_ERROR(CYLF_DEF, "Failed to parse date and time string. Expected 6 items, got %d.\r\n", result);
        return false;
    }
}

/*******************************************************************************
* Function Name: handle_datetime_command
********************************************************************************
* Summary:
*  Processes set date-time MQTT command.
*
* Parameters:
*  value - Date-time string value.
*
* Return:
*  void
*
*******************************************************************************/
static void handle_datetime_command(char *value)
{
    DateTime parsed_time = {0};

    if(true == parse_datetime_string(value, &parsed_time))
    {
        send_response_numeric(SET_DATE_TIME, OPERATION_RESPONSE, (uint32_t)MQTT_PARSER_SUCCESS);

        /* Update parsed date time info. on UI */
        set_datetime_ui(&parsed_time);
    }
    else
    {
        send_response_numeric(SET_DATE_TIME, OPERATION_RESPONSE, (uint32_t)MQTT_PARSER_JSON_PARSE_ERROR);
    }
}

/*******************************************************************************
* Function Name: handle_mqtt_command_str
********************************************************************************
* Summary:
*  Dispatches MQTT commands with string values.
*
* Parameters:
*  command - The MQTT command ID.
*  type - Operation type (read/write).
*  value - The string value.
*
* Return:
*  void
*
*******************************************************************************/
static void handle_mqtt_command_str(mqtt_commandId_e command, operation_type_e type, char *value)
{
    switch(command)
    {
        case SET_DATE_TIME:
            LOG_INFO(CYLF_DEF, "Handling SET_DATE_TIME...\n");
            handle_datetime_command(value);
            break;

        default:
            LOG_INFO(CYLF_DEF, "Un-handled string values.\n");
            break;
    }
}


/*******************************************************************************
* Function Name: handle_devicemode_command
********************************************************************************
* Summary:
*  Sets or gets the thermostat device mode.
*
* Parameters:
*  type - Operation type (read/write).
*  mode - The thermostat mode to set.
*
* Return:
*  void
*
*******************************************************************************/
static void handle_devicemode_command(operation_type_e type, thermostat_mode_t mode)
{

    /* Send device mode to the M55 for UI update */
    if(OPERATION_READ == type)
    {
        /* send mode */
        send_response_numeric(DEVICE_MODE, OPERATION_READ, (uint32_t)device_status.thermostat_settings.mode);
    }
    else if(OPERATION_WRITE == type)
    {
        /* Call Set fan API */
        send_response_numeric(DEVICE_MODE, OPERATION_RESPONSE, (uint32_t)MQTT_PARSER_SUCCESS);
        device_status.thermostat_settings.mode = mode;
        if(MODE_OFF == mode)
        {
            device_status.thermostat_settings.fan_speed = FAN_OFF;
            set_fan_speed(FAN_OFF);
        }
        set_device_mode(mode);
    }
}

/*******************************************************************************
* Function Name: handle_gettergettemp_command
********************************************************************************
* Summary:
*  Responds with the current target temperature.
*
* Parameters:
*  void
*
* Return:
*  void
*
*******************************************************************************/
static void handle_gettergettemp_command(void)
{
    send_response_numeric(TARGETED_TEMP, OPERATION_READ, (uint32_t)device_status.environment.target_temp);
}

/*******************************************************************************
* Function Name: handle_settempupdown_command
********************************************************************************
* Summary:
*  Adjusts temperature up or down by one unit.
*
* Parameters:
*  updown - Direction to adjust temperature.
*
* Return:
*  void
*
*******************************************************************************/
static void handle_settempupdown_command(temp_updown_e updown)
{
    /* Call set temp by +-1 API */

    (updown) ? device_status.environment.current_temp++ : device_status.environment.current_temp--;
    device_status.environment.target_temp = device_status.environment.current_temp;
    send_response_numeric(SETDEVICE_TEMP_UPDOWN, OPERATION_RESPONSE, (uint32_t)MQTT_PARSER_SUCCESS);
    set_device_temp(device_status.environment.current_temp);
}

/*******************************************************************************
* Function Name: handle_fanspeed_command
********************************************************************************
* Summary:
*  Sets or gets the current fan speed.
*
* Parameters:
*  type - Operation type (read/write).
*  speed - The fan speed to set.
*
* Return:
*  void
*
*******************************************************************************/
static void handle_fanspeed_command(operation_type_e type, fan_speed_t speed)
{
    if(OPERATION_READ == type)
    {
        send_response_numeric(FAN_SPEED, OPERATION_READ, (uint32_t)device_status.thermostat_settings.fan_speed);
    }
    else if(OPERATION_WRITE == type)
    {
        /* Call Set fan API */
        device_status.thermostat_settings.fan_speed = (fan_speed_t)speed;
        send_response_numeric(FAN_SPEED, OPERATION_RESPONSE, (uint32_t)MQTT_PARSER_SUCCESS);
        set_fan_speed(speed);
    }
}

/*******************************************************************************
* Function Name: handle_currenttemp_command
********************************************************************************
* Summary:
*  Sets or reads the current temperature value.
*
* Parameters:
*  type - Operation type (read/write).
*  value - The temperature value to set.
*
* Return:
*  void
*
*******************************************************************************/
void handle_currenttemp_command(operation_type_e type, uint32_t value)
{
    if(OPERATION_WRITE == type)
    {
        device_status.environment.current_temp = device_status.environment.target_temp = value;
        send_response_numeric(CURRENT_TEMP, OPERATION_RESPONSE, (uint32_t)MQTT_PARSER_SUCCESS);
        set_device_temp(value);
    }
    else if(OPERATION_READ == type)
    {
        /* add check for invalid temp */
        send_response_numeric(CURRENT_TEMP, OPERATION_READ, (uint32_t)device_status.environment.current_temp);
    }
}

/*******************************************************************************
* Function Name: handle_getcurrentpara_command
********************************************************************************
* Summary:
*  Retrieves the current humidity or CO2 level reading.
*
* Parameters:
*  cmd - The MQTT command ID specifying which parameter.
*
* Return:
*  void
*
*******************************************************************************/
void handle_getcurrentpara_command(mqtt_commandId_e cmd)
{
    int currentpara = 0;
    switch (cmd)
    {
        case GETDEVICE_CURRENT_HUMIDITY:
        {
            currentpara = device_status.environment.current_humidity;
            break;
        }
        default:
        {
            LOG_ERROR(CYLF_DEF, "Unknown parameter!");
            break;
        }
    }
    send_response_numeric(cmd, OPERATION_READ, (uint32_t)currentpara);
}

/*******************************************************************************
* Function Name: handle_brightness_command
********************************************************************************
* Summary:
*  Sets or gets the display brightness level.
*
* Parameters:
*  type - Operation type (read/write).
*  brightness - The brightness level to set.
*
* Return:
*  void
*
*******************************************************************************/
static void handle_brightness_command(operation_type_e type, uint8_t brightness)
{
    if(OPERATION_READ == type)
    {
        send_response_numeric(DEVICE_BRIGHTNESS, OPERATION_READ, (uint32_t)device_status.preferences.display_brightness);
    }
    else if(OPERATION_WRITE == type)
    {
        device_status.preferences.display_brightness = brightness;
        send_response_numeric(DEVICE_BRIGHTNESS, OPERATION_RESPONSE, (uint32_t)MQTT_PARSER_SUCCESS);
        set_device_brightness(brightness);
    }
}

/*******************************************************************************
* Function Name: handle_deviceaudio_command
********************************************************************************
* Summary:
*  Sets or gets the device audio volume level.
*
* Parameters:
*  type - Operation type (read/write).
*  level - The audio volume level.
*
* Return:
*  void
*
*******************************************************************************/
static void handle_deviceaudio_command(operation_type_e type, uint8_t level)
{
    if(OPERATION_READ == type)
    {
        send_response_numeric(DEVICE_AUDIO, OPERATION_READ, (uint32_t)device_status.preferences.audio_level);
    }
    else if(OPERATION_WRITE == type)
    {
        /* Call Set audio level */
        device_status.preferences.audio_level = (audio_level_t)level;
        send_response_numeric(DEVICE_AUDIO, OPERATION_RESPONSE, (uint32_t)MQTT_PARSER_SUCCESS);
        set_device_audio((audio_level_t)level);
    }
}


/*******************************************************************************
* Function Name: handle_pairingremove_command
********************************************************************************
* Summary:
*  Handles device unpairing and deprovisioning request.
*
* Parameters:
*  only_wifi - true to only reset Wi-Fi credentials.
*
* Return:
*  void
*
*******************************************************************************/
void handle_pairingremove_command(bool only_wifi)
{
    if(only_wifi)
    {
        send_response_numeric(DEVICE_STATUS, OPERATION_READ, (uint32_t)0);
    }
    else
    {
        send_response_numeric(DEVICE_PAIRING_DISCONECT, OPERATION_RESPONSE, (uint32_t)MQTT_PARSER_SUCCESS);
    }

    vTaskDelay(500);

    mqtt_task_cmd_t mqtt_task_cmd = HANDLE_DEPROVISIONING;
    is_cloud_connection_cancel = true;
    xQueueSend(mqtt_task_q, &mqtt_task_cmd, portMAX_DELAY);
}

/*******************************************************************************
* Function Name: handle_getdeviceconfig_command
********************************************************************************
* Summary:
*  Builds and sends full device configuration as JSON.
*
* Parameters:
*  void
*
* Return:
*  void
*
*******************************************************************************/
static void handle_getdeviceconfig_command(void)
{
    char temp[5] = {0};
    cJSON *json = cJSON_CreateObject();
    char *json_data = NULL;

    if(NULL != json)
    {
        snprintf(temp, sizeof(temp), "%d",DEVICE_STATUS);
        if(NULL == cJSON_AddNumberToObject(json, (const char *)temp, (uint32_t)true))
        {
            /* Can display error to user */
            cJSON_Delete(json);
            return;
        }

        snprintf(temp, sizeof(temp), "%d",CURRENT_TEMP);
        if(NULL == cJSON_AddNumberToObject(json, (const char *)temp, (uint32_t)device_status.environment.current_temp))
        {
            /* Can display error to user */
            cJSON_Delete(json);
            return;
        }

        snprintf(temp, sizeof(temp), "%d",TARGETED_TEMP);
        if(NULL == cJSON_AddNumberToObject(json, (const char *)temp, (uint32_t)device_status.environment.target_temp))
        {
            cJSON_Delete(json);
            return;
        }

        snprintf(temp, sizeof(temp), "%d",GETDEVICE_CURRENT_HUMIDITY);
        if(NULL == cJSON_AddNumberToObject(json, (const char *)temp, (uint32_t)device_status.environment.current_humidity))
        {
            cJSON_Delete(json);
            return;
        }

        snprintf(temp, sizeof(temp), "%d",DEVICE_MODE);
        if(NULL == cJSON_AddNumberToObject(json, (const char *)temp, (uint32_t)device_status.thermostat_settings.mode))
        {
            cJSON_Delete(json);
            return;
        }

        snprintf(temp, sizeof(temp), "%d",FAN_SPEED);
        if(NULL == cJSON_AddNumberToObject(json, (const char *)temp, (uint32_t)device_status.thermostat_settings.fan_speed))
        {
            cJSON_Delete(json);
            return;
        }

        snprintf(temp, sizeof(temp), "%d",DEVICE_BRIGHTNESS);
        if(NULL == cJSON_AddNumberToObject(json, (const char *)temp, (uint32_t)device_status.preferences.display_brightness))
        {
            cJSON_Delete(json);
            return;
        }

        snprintf(temp, sizeof(temp), "%d",DEVICE_AUDIO);
        if(NULL == cJSON_AddNumberToObject(json, (const char *)temp, (uint32_t)device_status.preferences.audio_level))
        {
            cJSON_Delete(json);
            return;
        }

        snprintf(temp, sizeof(temp), "%d",TEMP_UNIT);
        if(NULL == cJSON_AddNumberToObject(json, (const char *)temp, (uint32_t)device_status.thermostat_settings.temp_unit))
        {
            cJSON_Delete(json);
            return;
        }

        snprintf(temp, sizeof(temp), "%d",CURRENT_FIRMWARE_VERSION);
        if(NULL == cJSON_AddStringToObject(json, (const char *)temp, current_fw_version))
        {
            cJSON_Delete(json);
            return;
        }

        json_data = cJSON_Print(json);
        cJSON_Delete(json);

        if(NULL != json_data)
        {
            if(CONNECTIVITY_MQTT_CLOUD == current_interface)
            {
                if(!(get_mqtt_status() & FLAG_MQTT_CONNECTION_SUCCESS))
                {
                    LOG_INFO(CYLF_DEF, "Cloud not connected\n");
                    return;
                }

                publisher_q_data.cmd = PUBLISH_MQTT_MSG;
                publisher_q_data.data = json_data;

                xQueueSend(publisher_task_q, &publisher_q_data, portMAX_DELAY);
            }
            else if(CONNECTIVITY_BLE == current_interface)
            {
                app_bt_send_message(json_data, strlen(json_data));
            }
            else
            {
                LOG_INFO(CYLF_DEF, "No interface Found for data exchange!\n");
            }
        }
        else
        {
            LOG_INFO(CYLF_DEF, "Unable to create a JSON string!\n");
        }
    }
}

/*******************************************************************************
* Function Name: handle_tempunit_command
********************************************************************************
* Summary:
*  Sets or gets the temperature unit preference.
*
* Parameters:
*  type - Operation type (read/write).
*  unit - The temperature unit to set.
*
* Return:
*  void
*
*******************************************************************************/
void handle_tempunit_command(operation_type_e type, temp_unit_t unit)
{
    if(OPERATION_READ == type)
    {
        send_response_numeric(TEMP_UNIT, OPERATION_READ, (uint32_t)device_status.thermostat_settings.temp_unit);
    }
    else if(OPERATION_WRITE == type)
    {
        device_status.thermostat_settings.temp_unit = (temp_unit_t)unit;
        send_response_numeric(TEMP_UNIT, OPERATION_RESPONSE, (uint32_t)MQTT_PARSER_SUCCESS);
        set_device_temp_unit(unit);
    }
}

/*******************************************************************************
* Function Name: send_response_numeric
********************************************************************************
* Summary:
*  Sends a numeric MQTT response as JSON via the active interface.
*
* Parameters:
*  cmd - The MQTT command ID.
*  type - Operation type.
*  response - The numeric response value.
*
* Return:
*  void
*
*******************************************************************************/
void send_response_numeric(mqtt_commandId_e cmd, operation_type_e type, uint32_t response)
{
    jsonpyload_t pyload = {0};
    pyload.is_numeric = true;
    pyload.command = cmd;
    pyload.type = type;
    pyload.value = (double)response;

    if (xSemaphoreTake(uplink_mutex, pdMS_TO_TICKS(500)) == pdTRUE)
    {
        char *json_data = json_create_response(pyload);
        if( NULL == json_data )
        {
            LOG_INFO(CYLF_DEF, "send_response_numeric unable to create JSON instance\n");
            xSemaphoreGive(uplink_mutex);
            return;
        }
        else if(CONNECTIVITY_MQTT_CLOUD == current_interface)
        {
            if(!(get_mqtt_status() & FLAG_MQTT_CONNECTION_SUCCESS))
            {
                LOG_INFO(CYLF_DEF, "send_response_numeric Cloud not connected\n");

                /* Release Mutex */
                free(json_data);
                xSemaphoreGive(uplink_mutex);
                return;
            }

            publisher_q_data.cmd = PUBLISH_MQTT_MSG;
            publisher_q_data.data = json_data;
            if(pdTRUE != xQueueSend(publisher_task_q, &publisher_q_data, portMAX_DELAY))
            {
                free(json_data);
            }
        }
        else if(CONNECTIVITY_BLE == current_interface)
        {
            app_bt_send_message(json_data, strlen(json_data));
        }
        else
        {
            LOG_INFO(CYLF_DEF, "No interface Found for data exchange!\n");
            free(json_data);
        }

        /* Release Mutex */
        xSemaphoreGive(uplink_mutex);
    }
}

/*******************************************************************************
* Function Name: send_response_String
********************************************************************************
* Summary:
*  Sends a string MQTT response as JSON via the active interface.
*
* Parameters:
*  cmd - The MQTT command ID.
*  type - Operation type.
*  string_data - The string response value.
*
* Return:
*  void
*
*******************************************************************************/
void send_response_String(mqtt_commandId_e cmd, operation_type_e type, char *string_data)
{
    jsonpyload_t pyload = {0};
    pyload.is_numeric = false;
    pyload.command = cmd;
    pyload.type = type;
    pyload.buff = string_data;
    char *json_data = json_create_response(pyload);

    if(json_data != NULL)
    {
        if(CONNECTIVITY_MQTT_CLOUD == current_interface)
        {
            if(!(get_mqtt_status() & FLAG_MQTT_CONNECTION_SUCCESS))
            {
                LOG_INFO(CYLF_DEF, "send_response_String Cloud not connected\n");
                free(json_data);
                return;
            }

            publisher_q_data.cmd = PUBLISH_MQTT_MSG;
            publisher_q_data.data = json_data;
            if(pdTRUE != xQueueSend(publisher_task_q, &publisher_q_data, portMAX_DELAY))
            {
                free(json_data);
            }
        }
        else if(CONNECTIVITY_BLE == current_interface)
        {
            app_bt_send_message(json_data, strlen(json_data));
        }
        else
        {
            LOG_INFO(CYLF_DEF, "No interface Found for data exchange!\n");
            free(json_data);
        }
    }
    else
    {
        LOG_INFO(CYLF_DEF, "Unable to create a Json string!\n");
    }
}

/*******************************************************************************
* Function Name: handle_mqtt_command
********************************************************************************
* Summary:
*  Main dispatcher for numeric-value MQTT commands.
*
* Parameters:
*  command - The MQTT command ID.
*  type - Operation type (read/write).
*  value - The numeric value.
*
* Return:
*  void
*
*******************************************************************************/
static void handle_mqtt_command(mqtt_commandId_e command, operation_type_e type, uint32_t value)
{
    if((IS_BIT_SET(common_get_command, command)) && (OPERATION_READ != type))
    {
        /* Return with error if type of operation is invalid */
        send_response_numeric(command, OPERATION_RESPONSE, (uint32_t)MQTT_PARSER_INVALID_OPERATION_TYPE);
        return;
    }

    switch (command) {
        case DEVICE_STATUS:
            LOG_INFO(CYLF_DEF, "Handling DEVICE_STATUS...\n");
            handle_devicestatus_command();
            break;

        case DEVICE_MODE:
            LOG_INFO(CYLF_DEF, "Handling DEVICE_MODE...\n");
            handle_devicemode_command(type, value);
            break;

        case TARGETED_TEMP:
            LOG_INFO(CYLF_DEF, "Handling TARGETED_TEMP...\n");
            handle_gettergettemp_command();
            break;

        case SETDEVICE_TEMP_UPDOWN:
            LOG_INFO(CYLF_DEF, "Handling SETDEVICE_TEMP_UPDOWN...\n");
            handle_settempupdown_command(value); /* handle up or down */
            break;

        case FAN_SPEED:
            LOG_INFO(CYLF_DEF, "Handling FAN_SPEED...\n");
            handle_fanspeed_command(type, value);
            break;

        case CURRENT_TEMP:
            LOG_INFO(CYLF_DEF, "Handling GETDEVICE_CURRENT_TEMP...\n");
            handle_currenttemp_command(type, value);
            break;

        case GETDEVICE_CURRENT_HUMIDITY:
            LOG_INFO(CYLF_DEF, "Handling GETDEVICE_CURRENT_HUMIDITY...\n");
            handle_getcurrentpara_command(command);
            break;

        case DEVICE_BRIGHTNESS:
            LOG_INFO(CYLF_DEF, "Handling DEVICE_BRIGHTNESS...\n");
            handle_brightness_command(type, value);
            break;

        case DEVICE_AUDIO:
            LOG_INFO(CYLF_DEF, "Handling DEVICE_AUDIO...\n");
            handle_deviceaudio_command(type, value);
            break;

        case DEVICE_PAIRING_DISCONECT:
            LOG_INFO(CYLF_DEF, "Handling DEVICE_PAIRING_DISCONNECT...\n");
            handle_pairingremove_command(false);
            break;

        case TEMP_UNIT:
            LOG_INFO(CYLF_DEF, "Handling TEMP_UNIT...\n");
            handle_tempunit_command(type, value);
            break;

        case GET_DEVICE_CONFIGURATION:
            LOG_INFO(CYLF_DEF, "Handling GET_DEVICE_CONFIGURATION...\n");
            handle_getdeviceconfig_command();
            break;

        default:
            LOG_INFO(CYLF_DEF, "Unknown MQTT command received: %d\n", command);
            break;
    }
}

/**
 * Functions Definitions
 */

/*******************************************************************************
* Function Name: parse_received_command
********************************************************************************
* Summary:
*  Parses incoming MQTT JSON and dispatches the appropriate command.
*
* Parameters:
*  message - The incoming MQTT message string.
*  message_len - Length of the message.
*
* Return:
*  mqtt_parser_errors_e - Parser result status.
*
*******************************************************************************/
mqtt_parser_errors_e parse_received_command(const char *message, size_t message_len)
{
    double type, cmdId, value = 0;
    char *value_str;

    type = json_parsenumeric(message, JSON_KEY_FOR_TYPEOFOPERATION);
    if(type < 0)
    {
        return MQTT_PARSER_JSON_PARSE_ERROR;
    }
    cmdId = json_parsenumeric(message, JSON_KEY_FOR_COMMAND);
    if(cmdId < 0)
    {
        return MQTT_PARSER_JSON_PARSE_ERROR;
    }

    /* If cmd is date time, parse the string value  */
    if(SET_DATE_TIME == cmdId || CURRENT_FIRMWARE_VERSION == cmdId)
    {
        /* Parse the string value */
        value_str = json_parsestring(message, JSON_KEY_FOR_VALUE);
        handle_mqtt_command_str(cmdId, type, value_str);
    /* The memory allocated by json_parsestring must be freed. */
        if (value_str != NULL)
        {
            free(value_str);
        }
    }
    else
    {
        value = json_parsenumeric(message, JSON_KEY_FOR_VALUE);
        if(value < 0)
        {
            return MQTT_PARSER_JSON_PARSE_ERROR;
        }
        handle_mqtt_command(cmdId, type, (uint32_t)value);
    }

    return MQTT_PARSER_SUCCESS;
}


/*******************************************************************************
* Function Name: json_create_response
********************************************************************************
* Summary:
*  Creates a JSON response string from the payload structure.
*
* Parameters:
*  payload - The JSON payload structure.
*
* Return:
*  char* - Allocated JSON string, or NULL on failure.
*
*******************************************************************************/
static char *json_create_response(jsonpyload_t payload)
{
    cJSON *json = cJSON_CreateObject();

    if(NULL != json)
    {
        if(NULL == cJSON_AddNumberToObject(json, JSON_KEY_FOR_TYPEOFOPERATION, payload.type))
        {
            cJSON_Delete(json);
            return NULL;
        }

        if(NULL == cJSON_AddNumberToObject(json, JSON_KEY_FOR_COMMAND, payload.command))
        {
            cJSON_Delete(json);
            return NULL;
        }

        if(payload.is_numeric)
        {
            if(NULL == cJSON_AddNumberToObject(json, JSON_KEY_FOR_VALUE, payload.value))
            {
                cJSON_Delete(json);
                return NULL;
            }
        }
        else
        {
            if(NULL == payload.buff || NULL == cJSON_AddStringToObject(json, JSON_KEY_FOR_VALUE, payload.buff))
            {
                cJSON_Delete(json);
                return NULL;
            }
        }

        char *command_str = cJSON_Print(json);
        cJSON_Delete(json);
        return command_str;
    }
    else
    {
        LOG_INFO(CYLF_DEF, "Unable to generate response for %d command\n", (int)payload.command);
    }

    return NULL;
}

/*******************************************************************************
* Function Name: update_comm_interface
********************************************************************************
* Summary:
*  Updates the active communication interface type.
*
* Parameters:
*  void
*
* Return:
*  void
*
*******************************************************************************/
void update_comm_interface()
{
    if(get_ble_connection_status())
    {
        current_interface = CONNECTIVITY_BLE;
    }
    else if(get_mqtt_status() & FLAG_MQTT_CONNECTION_SUCCESS)
    {
        current_interface = CONNECTIVITY_MQTT_CLOUD;
    }
    else
    {
        current_interface = CONNECTIVITY_NONE;
    }
    LOG_INFO(CYLF_DEF, "Communication Interface Updated To : %d\n", current_interface);
}

/*******************************************************************************
* Function Name: send_device_config_info
********************************************************************************
* Summary:
*  Triggers sending full device configuration info.
*
* Parameters:
*  void
*
* Return:
*  void
*
*******************************************************************************/
void send_device_config_info(void)
{
    handle_getdeviceconfig_command();
}
