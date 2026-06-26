/*******************************************************************************
 * File Name:   thermostat_events.h
 *
 * Description:  Public interface for updating thermostat events on UI.
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

#ifndef THERMOSTAT_EVENTS_H
#define THERMOSTAT_EVENTS_H

#ifdef __cplusplus
extern "C" {
#endif

/*******************************************************************************
 *                                INCLUDES
 *******************************************************************************/

#include "ui/ui.h"
#include "app_common.h"
#include "utilities/app_rtc.h"

/*******************************************************************************
 *                                MACROS
 *******************************************************************************/

/*******************************************************************************
 *                                CONSTANTS
 *******************************************************************************/

/*******************************************************************************
 *                                DATA TYPES
 *******************************************************************************/

/* CO2 color code */ 
typedef enum {
    CO2_LVL_GOOD = 0x9BBA43,
    CO2_LVL_CAUTION = 0xFFC700,
    CO2_LVL_DANGER = 0xFF5A5A,
} co2_color_code_t;

typedef enum {
    STATE_NONE,
    STATE_HEATING,
    STATE_COOLING
}animation_state_t;

typedef enum {
    MIC_DISABLED,
    MIC_IDLE,
    MIC_ACTIVE
}mic_state_t;

typedef enum {
    NOTIFY_NETWORK_STATUS,
    NOTIFY_TEMP_UPDATE,
    NOTIFY_MODE_UPDATE,
    NOTIFY_FAN_MODE_UPDATE,
    NOTIFY_FW_UPDATE,
    NOTIFY_HTTP_SYNC,
    NOTIFY_HUM_WARNING,
    NOTIFY_HUM_VENTI,
    NOTIFY_HUM_VENTI_IMP,
    NOTIFY_CONNECTIVITY_TUTORIAL,
    NOTIFY_FAN_TUTORIAL
} notification_type;

typedef enum {
    NOTIF_SUCCESS,
    NOTIF_FAIL,
} notification_status_t;

typedef enum {
    PROV_MAPP_BLE = 1,
    PROV_UI_KEYBOARD
} provisioning_method_t;

typedef struct {
    notification_type type;
    notification_status_t status;
    uint32_t value;
} notification_data_t;

typedef enum
{
    TEMP_MIN = 14,
    TEMP_MAX = 30
} temperature_range_t;

typedef enum
{
    CLEAR,
    CLOUDY,
    FOG,
    RAIN,
    SNOW,
    THUNDER
}weather_t;

typedef enum{
    DESC_RIGHT_PANEL,
    DESC_LEFT_PANEL,
    DESC_ARC,
    DESC_PRESENCE,
    DESC_COMMAND
} tutorial_descriptions_t;

/*******************************************************************************
 *                                GLOBAL VARIABLES
 *******************************************************************************/
extern lv_timer_t *temp_timer;
extern lv_timer_t *tutorialTimer;
extern uint8_t brightness_level;
extern uint8_t audio_level;
extern char device_unique_id[13];
extern device_settings_t current_settings;
extern volatile application_state_t app_state;
extern volatile bool popup_overlay_visible;
extern volatile bool is_device_connected;
extern device_state_t dev_info;
extern volatile bool hide_conn_screen;
extern bool is_mic_clicked;
extern bool voice_popup_state;
extern volatile bool wifi_popup_state;
extern bool fw_update_timeout;
extern bool is_new_fw_available;
extern char weather_code_sync_value[8];
extern bool is_device_provisioned;
extern volatile bool is_http_fetching;
extern volatile bool is_first_http_fetch;
extern char weather_sync_value[32];
extern char location_sync_value[32];
extern char hour_sync_value[8];
extern char minute_sync_value[8];
extern char second_sync_value[8];
extern char date_sync_value[8];
extern char month_sync_value[8];
extern char vaar_sync_value[8];
extern char year_sync_value[8];
extern DateTime rx_datetime;
extern rtc_time_t now;

/*******************************************************************************
 *                                FUNCTION PROTOTYPES
 *******************************************************************************/
void hide_presence_icon(void);
void update_presence_detection(uint8_t presence_count);
void mic_icon_click_handler(lv_event_t * e);
void display_presence_detection_status(void);
static inline int clip_angle(int angle);
void wifi_status_handler(void);
void update_weather_icon(weather_t weather_icon);
int is_fahrenheit_resolved(void);
void start_ble_adv(lv_event_t *e);
void connect_wifi(lv_event_t * e);
void update_wifi_cred(lv_event_t *e);
void toggle_mode(lv_event_t * e);
void update_thermostat_mode(thermostat_mode_t mode);
void update_device_temp(uint8_t temp);
void set_thermostat_mode(thermostat_mode_t mode);
fan_speed_t get_current_fan_mode(void);
void update_fan_mode(fan_speed_t mode);
void fan_clicked(lv_event_t * e);
thermostat_mode_t get_current_device_mode(void);
uint8_t get_current_brigthness(void);
int get_current_temperature(void);
void update_notifcation_label(notification_type type, notification_status_t status, uint32_t value);
void load_thermostat_config(thermostat_mode_t mode);
void change_idle_timeout(lv_event_t * e);
void update_temperature(lv_event_t * e);
void update_final_temperature(lv_event_t *e);
void update_thermostat_mode_timer(void);
void set_idle_timeout(idle_timeout_t time);
void change_volume(lv_event_t * e);
void update_thermostat_volume(audio_level_t level);
void set_system_unit(lv_event_t * e);
void update_system_unit(temp_unit_t unit);
void get_default_device_setting(device_settings_t *settings);
void get_current_device_setting(device_settings_t *settings);
void set_current_device_setting(device_settings_t *settings);
void update_current_device_setting(void);
void device_factory_reset(lv_event_t * e);
void update_device_config_ipc(void);
void start_inactivity_timer(void);
void start_inactivity_timer_addn(uint32_t additional_timeout_ms);
void cancel_dev_conn_current_opt(lv_event_t *e);
void auto_reconnect_cb(lv_event_t *e);
void switch_to_wifi(lv_event_t *e);
void switch_to_active_screen(void);
void switch_to_ble(lv_event_t *e);
void update_date_time_rtc(lv_event_t * e);
void display_fan_anim(void);
void stop_fan_anim(void);
void update_homescreen_connectivity_state(void);
void stop_homescreen_connectivity_state(void);
void display_temp_change_anim(void);
void enqueue_notification(notification_type type, notification_status_t status, uint32_t value);
void ui_timer_init(void);
void ui_timer_stop(void);
void ui_timer_start(void);
void hide_connectivity_screen(void);
void switchDescMessagecb(lv_timer_t * timer);
void setDescArc(void);
void setDescLeftPanel(void);
void setDescRightPanel(void);
void setDescCommand(void);
void setDescPresence(void);
void descHideAll(void);
void nextDescription(void);
void toggleTutorial(void);
void update_date_via_http(void);
void sync_ui_via_rtc(void);
void sync_ui_via_http(int year, int month, int day, int hour, int minute, int second);
void sync_ui_via_ble_or_mqtt(DateTime rx_datetime);
void handle_http_factory_reset(void);
static void hide_popup_timer_cb(lv_timer_t *timer);
#ifdef __cplusplus
}
#endif

#endif /* THERMOSTAT_EVENTS_H */

/* [] END OF FILE */
