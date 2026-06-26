/******************************************************************************
* File Name:   mqtt_command_handler.h
*
* Description:  Header file for MQTT command parsing and creation.
*                 Provides APIs to parse incoming MQTT messages and construct response commands.
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

#ifndef MQTT_MQTT_COMMAND_HANDLER_H_
#define MQTT_MQTT_COMMAND_HANDLER_H_


#ifdef __cplusplus
extern "C" {
#endif
/*******************************************************************************
 *                                INCLUDES
 *****************************************************************************/
#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <cJSON.h>
#include "app_common.h"
#include "wireless_manager.h"

/*******************************************************************************
 *                          GLOBAL DECLARATIONS
 *****************************************************************************/
typedef enum mqtt_commandId
{
    DEVICE_STATUS                 = 0,
    DEVICE_MODE                    = 1,
    TARGETED_TEMP                = 2,
    SETDEVICE_TEMP_UPDOWN        = 3,
    FAN_SPEED                    = 4,
    CURRENT_TEMP                = 5,
    GETDEVICE_CURRENT_HUMIDITY    = 6,
    GETDEVICE_CURRENT_CO2LEVEL    = 7,
    DEVICE_BRIGHTNESS            = 8,
    DEVICE_AUDIO                = 9,
    DEVICE_FIRMWARE_UPDATE        = 10,
    FIRMWARE_UPDATE_PROGRESS    = 11,
    FIRMWARE_UPDATE_STATUS        = 12,
    DEVICE_PAIRING_DISCONECT    = 13,
    CURRENT_FIRMWARE_VERSION    = 14,
    TIME_REMAINING                = 15,
    TEMP_UNIT                    = 16,
    GET_DEVICE_CONFIGURATION    = 17,
    DEVICE_SWITCH_TO_BLE        = 18,
    SET_DATE_TIME               = 19,
    DEVICE_SWITCH_TO_WIFI       = 20,

    MAX_COMMAND_ID
} mqtt_commandId_e;

// Enum for MQTT processing errors
typedef enum {
    MQTT_PARSER_SUCCESS = 0,             // No error
    MQTT_PARSER_NULL_INPUT_ERROR,        // Null input string
    MQTT_PARSER_EMPTY_INPUT_ERROR,       // Input string is empty
    MQTT_PARSER_JSON_PARSE_ERROR,        // Malformed JSON
    MQTT_PARSER_MEMORY_ALLOCATION_ERROR, // Memory allocation failure
    MQTT_PARSER_OBJECT_CREATION_ERROR,   // Failed to create JSON object
    MQTT_PARSER_KEY_NOT_FOUND_ERROR,     // Expected key missing
    MQTT_PARSER_TYPE_MISMATCH_ERROR,     // Key found but with unexpected type
    MQTT_PARSER_VALUE_OUT_OF_RANGE_ERROR,// Value exceeds allowed limits or bounds
    MQTT_PARSER_INVALID_COMMAND_ID,      // Unknown or unsupported command ID
    MQTT_PARSER_INVALID_OPERATION_TYPE,  // Read/Write operation type invalid
    MQTT_PARSER_ENCODING_ERROR,          // JSON string encoding failure
    MQTT_PARSER_BUILD_ERROR,             // Error during JSON construction
    MQTT_PARSER_UNKNOWN_ERROR            // Fallback for undefined issues
} mqtt_parser_errors_e;

// Enum for operation types
typedef enum operation_type{
    OPERATION_READ     = 0,      // Read operation
    OPERATION_WRITE = 1,      // Write operation
    OPERATION_RESPONSE = 2, // response operation

    MAX_OPERATION
} operation_type_e;

// Enum for temperature up/down
typedef enum temp_updown{
    DECREASE     = 0,
    INCREASE    = 1,

    MAX_TEMP_UPDOWN
} temp_updown_e;


extern device_state_t device_status;
/**
 * Functions declarations
 */
/*******************************************************************************
* Function Name: parse_received_command
********************************************************************************
* Summary:
* Parses the incoming MQTT message and extracts the command ID and associated parameters.
*
* Parameters:
*  message     - Pointer to the raw JSON-formatted MQTT message (const char*)
*  message_len - Length of the message in bytes (size_t)
*
* Return:
*  mqtt_parser_errors_e - Status code indicating success or type of parsing error
*******************************************************************************/
mqtt_parser_errors_e parse_received_command(const char *message, size_t message_len);

/*******************************************************************************
* Function Name: send_response_numeric
********************************************************************************
* Summary:
* Sends a response back over MQTT with the specified command, type, and response value.
*
* Parameters:
*  cmd      - Command ID to respond to (mqtt_commandId_e)
*  type     - Operation type (operation_type_e)
*  response - Numeric response value (uint32_t)
*
* Return:
*  void
*******************************************************************************/
void send_response_numeric(mqtt_commandId_e cmd, operation_type_e type, uint32_t response);

/*******************************************************************************
* Function Name: handle_pairingremove_command
********************************************************************************
* Summary:
* Handles the DEVICE_PAIRING_DISCONNECT command to unpair or reset Bluetooth/Wi-Fi pairing.
*
* Parameters:
*  only_wifi - If true, only Wi-Fi pairing is removed (bool)
*
* Return:
*  void
*******************************************************************************/
void handle_pairingremove_command(bool only_wifi);

/*******************************************************************************
* Function Name: update_comm_interface
********************************************************************************
* Summary:
* Updates the current interface used for communication with the mobile app.
*
* Parameters:
*  void
*
* Return:
*  void
*******************************************************************************/
void update_comm_interface(void);

/*******************************************************************************
* Function Name: send_device_config_info
********************************************************************************
* Summary:
* Sends the current device configuration information over MQTT.
*
* Parameters:
*  void
*
* Return:
*  void
*******************************************************************************/
void send_device_config_info(void);

/*******************************************************************************
* Function Name: handle_firmwareupdateprogress_command
********************************************************************************
* Summary:
* Processes the FIRMWARE_UPDATE_PROGRESS command to send update progress details.
*
* Parameters:
*  per - Firmware update progress percentage (uint8_t)
*
* Return:
*  void
*******************************************************************************/
void handle_firmwareupdateprogress_command(uint8_t per);

#ifdef __cplusplus
}
#endif

#endif /* MQTT_MQTT_COMMAND_HANDLER_H_ */
