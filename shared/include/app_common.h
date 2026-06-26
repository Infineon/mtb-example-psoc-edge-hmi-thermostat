/*******************************************************************************
 * File Name:   app_common.h
 *
 * Description:  Public interface for common functionality between multiple cores.
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

#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include "app_config.h"
#include "ipc_communication.h"
#include "cy_device.h"
#include "cy_log.h"

#ifndef SHARED_INCLUDE_APP_COMMON_H_
#define SHARED_INCLUDE_APP_COMMON_H_


#ifdef __cplusplus
extern "C" {
#endif
/*******************************************************************************
 *                                Macros
 *******************************************************************************/
#define APP_BOOTUP_DELAY    2500U
#define APP_RRAM_NVM_MAIN_NS_START        0x22000000
#define APP_NVM_DEVICE_SETTINGS_OFFSET      0x00002000

#define MAX_FW_VERSION_LEN                  (10U)

#define RTOS_STACK_SIZE_IN_BYTE(x)             ((x)/4U)

#define UNUSED_PARAM(x) (void)(x)

// Macro for logging an INFO message
#define LOG_INFO(tag, msg, ...) \
    cy_log_msg(tag, CY_LOG_INFO, msg, ##__VA_ARGS__)

// Macro for logging an ERROR message
#define LOG_ERROR(tag, msg, ...) \
    cy_log_msg(tag, CY_LOG_ERR, msg, ##__VA_ARGS__)

// Macro for logging a DEBUG message
#define LOG_DEBUG(tag, msg, ...) \
    cy_log_msg(tag, CY_LOG_DEBUG, msg, ##__VA_ARGS__)


/*******************************************************************************
 *                                Data Types
 *******************************************************************************/
/* Function pointer for event handler */
typedef void (*EventHandler)(void*);

/* Application State */
typedef enum {
    APP_ST_ACTIVE,                /* Both CM33 and CM55 active */
    APP_ST_IDLE,                /* Both CM33 and CM55 active, with UI displaying Idle screen */
    APP_ST_STANDBY                /* Only CM33 active */
} application_state_t;

/* CM55 to CM33 */
typedef enum {
    CMD_CM55_UI_CONTROL,
    CMD_CM55_VOICE
} notify_type_t;

/* CM33 to CM55 */
typedef enum {
    DEVICE_STARTED_BLE_ADV,
    DEVICE_WIFI_CONN_SATRTED,
    DEVICE_WIFI_CONNECTED,
    DEVICE_MQTT_CONNECTED,
} device_status_t;

/*Connectivity Interface*/
typedef enum {
    BLE_INTERFACE,
    WIFI_INTERFACE,
    CLOUD_INTERFACE,

    MAX_INTERFACE,
} connectivity_interface_t;

/* Event Notifier types  */
typedef enum {
    EVT_USER_BUTTON,
    EVT_UI_UPDATE,
    EVT_BLE_WIFI_CTRL,
    EVT_BLE_START_ADV,
    EVT_WIFI_CONNECT,

    NOTIF_UNKNOWN
} event_type_t;

/* CMDs evts from CM33 to CM55 */
typedef enum {
    UI_UPDATE_BLE_ADV_START,
    UI_UPDATE_BLE_CONNECTED,
    UI_UPDATE_WIFI_CONNECTING,
    UI_UPDATE_WIFI_CONNECTED,
    UI_UPDATE_WIFI_DISCONNECTED,
    UI_UPDATE_CLOUD_CONNECTED,
} ui_update_evt_t;

typedef enum {
    FAN_OFF = 0,
    FAN_HIGH,
    FAN_MED,
    FAN_LOW,
    FAN_MODE_MAX
}fan_speed_t;


typedef enum {
    MODE_ECO = 0,
    MODE_RAPID,
    MODE_AUTO,
    MODE_OFF,
    MODE_MAX
} thermostat_mode_t;

typedef enum {
    // BLE Events
    EVENT_BLE_ADVERTISING = 0,
    EVENT_BLE_SCANNING,
    EVENT_BLE_PAIRING_PIN_REQUEST,
    EVENT_BLE_PAIRED,

    // Wi-Fi Events
    EVENT_WIFI_SCANNING,
    EVENT_WIFI_CONNECTING,
    EVENT_WIFI_CONNECTED,

    // Cloud Events
    EVENT_CLOUD_CONNECTING,
    EVENT_CLOUD_CONNECTED,

    EVENT_MAX
} app_event_id_e;

typedef enum {
    DEV_ST_CLOUD_STATE_MIN,
    DEV_ST_CLOUD_CONNECTED = DEV_ST_CLOUD_STATE_MIN,
    DEV_ST_CLOUD_CONNECTING,
    DEV_ST_CLOUD_CONNECTION_CANCEL,
    DEV_ST_CLOUD_DISCONNECTED,
    DEV_ST_CLOUD_STATE_MAX = DEV_ST_CLOUD_DISCONNECTED,

    DEV_ST_WIFI_STATE_MIN,
    DEV_ST_WIFI_CONNECTING = DEV_ST_WIFI_STATE_MIN,
    DEV_ST_WIFI_CONNECTED,
    DEV_ST_WIFI_CONNECTION_CANCEL,
    DEV_ST_WIFI_DISCONNECTED,
    DEV_ST_SWITCH_WIFI,
    DEV_ST_WIFI_STATE_MAX = DEV_ST_SWITCH_WIFI,

    DEV_ST_BLE_STATE_MIN,
    DEV_ST_BLE_ADVERTISING = DEV_ST_BLE_STATE_MIN,
    DEV_ST_BLE_ADVERTISEMENT_CANCEL,
    DEV_ST_BLE_PAIRING,
    DEV_ST_BLE_CONNECTED,
    DEV_ST_BLE_STATE_MAX = DEV_ST_BLE_CONNECTED,

    DEV_ST_UNPROVISIONED,
    DEV_ST_PROVISIONED,
    DEV_ST_SWITCH_BLE,
    DEV_ST_FW_HAVE_SAME_VERSION,
    DEV_ST_NO_INTERNET,
    DEV_ST_SYNCED_HTTP,
    
    DEV_ST_MAX
} device_connection_state_t;

typedef enum {
    SCREEN_MAIN,
    SCREEN_SETTINGS,
    SCREEN_FW,
    SCREEN_DATE_TIME,
    SCREEN_SETTINGS_SYSTEM,
    SCREEN_SETTINGS_AUDIO
} screen_id_t;

typedef enum {
    BLE_CONN_RETRY_LIMITED,
    BLE_CONN_RETRY_INFINITE,
    WIFI_CONN_RETRY_LIMITED,
    WIFI_CONN_RETRY_INFINITE,
    CLOUD_CONN_RETRY_LIMITED,
    CLOUD_CONN_RETRY_INFINITE,

    MAX_CONN_RETRY,
} connection_retries_t;

typedef enum {
    ABSENCE_DETECTED,
    PRESENCE_DETECTED,
} presence_status_t ;

typedef enum{
    LOW_HUMIDITY,
    HIGH_HUMIDITY,
} humidity_status_t;

typedef enum {
    IPC_CMD_CURRENT_EVENT = 0,              //Send a current event to UI

    IPC_CMD_GET_UID,                        //Request UID
    IPC_CMD_SET_UID,                        //Response of UID

    IPC_CMD_UPDATE_CONN_STATE,
    IPC_CMD_UPDATE_CONN_RETRIES,            //Connection retries

    IPC_CMD_UPDATE_PROVISION_STATE,

    // Wi-Fi
    IPC_CMD_SET_WIFI_SSID,                  // Set SSID string
    IPC_CMD_SET_WIFI_PASSWORD,              // Set Wi-Fi password
    IPC_CMD_SET_WIFI_SSID_PASS,              // Set Wi-Fi password
    IPC_CMD_RESET_WIFI_SSID_PASS,              // Erase Wi-Fi password
    IPC_CMD_UPDATE_PRESENCE_STATUS,                   //Update presence detection

    // Environmental data
    IPC_CMD_GET_CURRENT_TEMP,                // Get current temperature
    IPC_CMD_GET_CURRENT_HUMIDITY,           // Get current humidity
    IPC_CMD_SET_TARGET_TEMP,                // Set user-defined target temperature
    IPC_CMD_SET_CURRENT_TEMP,                // Set current temperature
    IPC_CMD_SET_TEMPERATURE_DATA,            //Set Current,Target and remaing time data

    IPC_CMD_DEVICE_CONFIG,                     // Device Config

    //TIME
    IPC_CMD_SET_REMAINING_TIME,               // Set remainig time to reach target temp.

    // HVAC settings
    IPC_CMD_SET_FAN_SPEED,                  // Set fan speed level
    IPC_CMD_GET_FAN_SPEED,                  // Get fan speed level
    IPC_CMD_SET_THERMOSTAT_MODE,            // Set operating mode
    IPC_CMD_GET_THERMOSTAT_MODE,            // Get operating mode

    IPC_CMD_SET_TEMP_UNIT,                     // Temperature unit
    IPC_CMD_GET_TEMP_UNIT,                     // Temperature unit

    // User preferences
    IPC_CMD_SET_DISPLAY_BRIGHTNESS,         // Set brightness level
    IPC_CMD_GET_DISPLAY_BRIGHTNESS,         // Get brightness level
    IPC_CMD_SET_AUDIO_LEVEL,                // Set audio output level
    IPC_CMD_GET_AUDIO_LEVEL,                // Get audio output level

    IPC_CMD_DISABLE_TOUCH,

    IPC_CMD_SWITCH_TO_BLE,
    IPC_CMD_SET_DATE_TIME,                  //Set date time

    IPC_CMD_UPDATE_CURRENT_SCREEN,          //Switch Screen
    IPC_CMD_NO_INTERNET_NOTIFY,             //No internet notification on UI

    IPC_CMD_MAX,
    IPC_CMD_WEATHER_SYNC,                // Weather sync
    IPC_CMD_LOCATION_SYNC,               // Location sync
    IPC_CMD_HOUR_SYNC ,                  // Time sync   
    IPC_CMD_MINUTE_SYNC,                 // Minute sync
    IPC_CMD_SECOND_SYNC,                 // Second sync
    IPC_CMD_DATE_SYNC,                   // Date sync
    IPC_CMD_MONTH_SYNC,                  // Month sync
    IPC_CMD_VAAR_SYNC,                   // Vaar sync
    IPC_CMD_YEAR_SYNC,                   // Year sync 
    IPC_CMD_WEATHER_CODE_SYNC,
    IPC_CMD_HTTP_FETCHING_START,         // HTTP data fetching in progress
    IPC_CMD_HTTP_LOCATION_FETCHING,      // Location data fetching with dashes
    IPC_CMD_HTTP_WEATHER_FETCHING,       // Weather data fetching with dashes
    IPC_CMD_HTTP_DATETIME_FETCHING,      // Date/time data fetching with dashes

} ipc_command_e;

// A simple structure to hold the parsed date and time components.
typedef struct {
    int year;
    int month;
    int day;
    int hour;
    int minute;
    int second;
} DateTime;


typedef enum  {
    AUDIO_OFF = 0,
    AUDIO_LOW,
    AUDIO_MED,
    AUDIO_HIGH,
}audio_level_t;

typedef enum {
    TIMEOUT_3S = 0,
    TIMEOUT_5S,
    TIMEOUT_10S,
    TIMEOUT_20S,
    TIMEOUT_30S,
    TIMEOUT_NEVER
} idle_timeout_t;

typedef enum  {
    TEMP_UNIT_CELSIUS = 0,
    TEMP_UNIT_FAHRENHEIT = 1,

    TEMP_UNIT_MAX,
}temp_unit_t;

typedef enum
{
    CONNECTIVITY_NONE       = 0,        // No active connectivity
    CONNECTIVITY_BLE        = 1,        // Bluetooth Low Energy
    CONNECTIVITY_MQTT_CLOUD = 2,        // Cloud via MQTT over Wi-Fi
} connectivity_medium_t;


/* Structure to hold WiFi details */
typedef struct {
    char ssid[33];     // SSID max length 32 + null
    char password[65]; // WPA2 max length 64 + null
} wifi_credentials_t;

/* Structure to hold BLE Pairing code details */
typedef struct {
    char pair_code[7];     // BLE Pairing code (6+1)
} ble_pairing_code_t;

/* Event structure */
typedef struct {
    event_type_t type;
    EventHandler Handler;
    uint8_t data;
    union {
        wifi_credentials_t    wifi_info;
        ble_pairing_code_t    pairing_code;
    };
} app_event_t;

typedef struct {
    uint32_t current_temp;
    uint32_t current_humidity;
    uint32_t target_temp;
} environmental_data_t;

typedef struct {
    uint32_t time_remains;
    fan_speed_t fan_speed;
    thermostat_mode_t mode;
    temp_unit_t temp_unit;
} thermostate_settings_t;

typedef struct {
    uint8_t display_brightness;
    audio_level_t audio_level;
} user_preferences_t;

typedef struct {
    wifi_credentials_t wifi;
    environmental_data_t environment;
    thermostate_settings_t thermostat_settings;
    user_preferences_t preferences;
} device_state_t;

typedef struct {
    uint8_t brightness;     /* UI brightness */
} display_settings_t;

typedef struct {
    audio_level_t level;    /* audio level */
} audio_settings_t;

typedef struct {
    fan_speed_t fan_mode;    /* fan mode */
    thermostat_mode_t mode;     /* thermostat mode */
} thermostat_settings_t;

typedef struct {
    uint8_t idle_timeout;    /* idle timeout */
    uint8_t temperature_unit;    /* Unit selection for temperature */
} system_settings_t;

typedef struct {
    display_settings_t  display_setting;            /* Display setting */
    thermostat_settings_t thermostat_setting;     /* Thermostat setting */
    audio_settings_t    audio;                    /* Audio settings */
    system_settings_t    system;                    /* System setting */
    uint8_t is_available;                        /* Flag to check if settings are available */
    bool isTutorial;                            /* Flag to run tutorials */
} device_settings_t;

uint32_t get_timeout_ms(idle_timeout_t timeout);

#ifdef __cplusplus
}
#endif

#endif /* SHARED_INCLUDE_APP_COMMON_H_ */
