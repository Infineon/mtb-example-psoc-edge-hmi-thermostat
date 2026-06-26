/*******************************************************************************
 * File Name:  thermostat_events.c
 *
 * Description:
 *
 * This file implements the Emulated EEPROM initialization, write
 * and read operation.
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
#include "thermostat_events.h"
#include "ui/ui_events.h"
#include "timers.h"
#include "qr_manager.h"
#include "lv_qrcode.h"
#include "comm_manager.h"
#include "ui/screens/ui_ActiveScreen.h"
#include "app_eeprom.h"
#include "app_sensor.h"
#include "app_speaker.h"
#include "lvgl.h"

/*******************************************************************************
 *                              CONSTANTS
 ******************************************************************************/
#define CM55_APP_DELAY_MS           (50U)
#define ECO_MODE_TEMP_TIMER_TIMEOUT     (5000U)
#define RAPID_MODE_TEMP_TIMER_TIMEOUT       (1000U)
#define AUTO_MODE_TEMP_TIMER_TIMEOUT        (2500U)
#define NOTIFICATION_TIMEOUT_MS     (1500U)

#define ECO_MODE_DEG_DELAY_SEC              (5)
#define RAPID_MODE_DEG_DELAY_SEC            (1)
#define AUTO_MODE_DEG_DELAY_SEC             (3)

#define WIFI_ILABEL_TIMER_TIMEOUT_MS (2000U)  /* Timeout in milliseconds (e.g., 2000ms = 2 seconds) */

#define TEMPERATURE_DEG_C_MAX_VALUE (30U)
#define TEMPERATURE_DEG_C_MIN_VALUE (14U)

#define TEMPERATURE_DEG_F_MAX_VALUE  ((30 * 9 / 5) + 32) /* 86°F */
#define TEMPERATURE_DEG_F_MIN_VALUE  ((14 * 9 / 5) + 32) /* 57.2°F ≈ 57°F */

#define TEMP_CONVERSION_CORRECTION_FACTOR 0.5

#define CELSIUS_TO_FAHRENHEIT(c)  ((int)((((float)(c) * 9.0 / 5.0) + 32.0) + TEMP_CONVERSION_CORRECTION_FACTOR))
#define FAHRENHEIT_TO_CELSIUS(f)  ((int)((((float)(f) - 32.0) * 5.0 / 9.0) + TEMP_CONVERSION_CORRECTION_FACTOR))

#define AUTO_MODE_TARGET_TEMP_DEFAULT_C (22U)
#define AUTO_MODE_TARGET_TEMP_DEFAULT_F CELSIUS_TO_FAHRENHEIT(AUTO_MODE_TARGET_TEMP_DEFAULT_C)

#define DEFAULT_STARTUP_TEMPERATURE    (24U)

#define MAX_NOTIF_QUEUE     (6U) /* Notification related */

#define TEMPERATURE_ARC_START_ANGLE (120U)
#define TEMPERATURE_ARC_END_ANGLE   (420U)
#define TEMPERATURE_ARC_ANGLE_PER_STEP (18U)

#define RH_THRESHOLD                                        (55)
#define LIMIT_RH                                            (0)
#define MAX_RH                                              (100)
#define INITAL_BRIGHTNESS_LEVEL             (100U)
/*******************************************************************************
 *                             GLOBAL VARIABLES
 ******************************************************************************/
bool is_mic_clicked = false;
volatile bool popup_overlay_visible = false;
volatile application_state_t app_state = APP_ST_ACTIVE;
device_state_t dev_info;

uint8_t current_max_temp = TEMPERATURE_DEG_C_MAX_VALUE;
uint8_t current_min_temp = TEMPERATURE_DEG_C_MIN_VALUE;
char subinfo[64] = { 0 };

char device_unique_id[13] = { 0 };
lv_timer_t *temp_timer = NULL;
lv_timer_t *start_listening_timer = NULL;
lv_timer_t *stop_listening_timer = NULL;
lv_timer_t *state_update_timer = NULL;

device_settings_t current_settings = { 0 };
bool is_device_provisioned = false;

/* Device connection state flag in CM55 core */
volatile bool is_device_connected = false;

/* Flag to check if the screen need to be hidden */
volatile bool hide_conn_screen = false;

/* Flag to prevent overlay dismiss during the auto-hide timer window */
static bool popup_auto_hide_pending = false;

static lv_timer_t *app_timer = NULL;
static lv_timer_t *lvgl_timer = NULL;
void start_active_state_timer(uint32_t timeout_ms);
void stop_active_state_timer(void);
volatile bool wifi_popup_state = false;
bool voice_popup_state = false;
bool fw_update_timeout = false;
bool is_new_fw_available = false;
connection_retries_t wifi_reconnect_retry     = WIFI_CONN_RETRY_LIMITED;
connection_retries_t cloud_reconnect_retry     = CLOUD_CONN_RETRY_LIMITED;
connection_retries_t ble_reconnect_retry     = BLE_CONN_RETRY_LIMITED;
bool switchTimer = true;
tutorial_descriptions_t current_widget = DESC_ARC;
lv_timer_t *tutorialTimer;

/*******************************************************************************
 *                             STATIC VARIABLES
 ******************************************************************************/

/* Default thermostat configuration */
static device_settings_t default_config =
{
        .audio.level = AUDIO_MED,
        .display_setting.brightness = 100,
        .thermostat_setting.fan_mode = FAN_LOW,
        .thermostat_setting.mode = MODE_ECO,
        .system.idle_timeout = TIMEOUT_20S,
        .system.temperature_unit = TEMP_UNIT_CELSIUS,
        .isTutorial = true,
};

static int temperature = DEFAULT_STARTUP_TEMPERATURE;
static int current_temp = DEFAULT_STARTUP_TEMPERATURE;
static int target_temp = DEFAULT_STARTUP_TEMPERATURE;
static mic_state_t current_mic_state = MIC_DISABLED;

/* Current thermostat mode */
static thermostat_mode_t dev_current_mode = MODE_ECO;

/* Current fan mode */
static fan_speed_t dev_fan_mode = FAN_OFF;

/* Current selected temperature unit */
static temp_unit_t dev_unit = TEMP_UNIT_CELSIUS;

static volatile uint32_t deg2sec = ECO_MODE_TEMP_TIMER_TIMEOUT; /**/

/* Timer handle */
static TimerHandle_t wifi_ilabel_timer = NULL;
static TimerHandle_t wifi_kybd_label_timer = NULL;
static TimerHandle_t fw_update_check_timer = NULL;

static provisioning_method_t prov_method = PROV_MAPP_BLE;
static animation_state_t current_state = STATE_NONE;

static bool person_detected = false;
static uint8_t person_count = 0;
static int weather_temp = 11;
static qr_manager_t qr_manager;

static notification_data_t notif_queue[MAX_NOTIF_QUEUE];
static int notif_head = 0;
static int notif_tail = 0;
static bool notif_showing = false;
static lv_timer_t *notif_timer = NULL;

int angle;
/* Device current and previous connection state */
device_connection_state_t dev_current_conn_state = DEV_ST_UNPROVISIONED;
device_connection_state_t dev_last_conn_state = DEV_ST_UNPROVISIONED;

/* Notification audio type */
static audio_type_t notify_audio_type = AUDIO_ERROR;

static volatile bool ble_conn_state = false;
static volatile bool wifi_conn_state = false;
static volatile bool cloud_conn_state = false;

uint8_t brightness_level = INITAL_BRIGHTNESS_LEVEL;
audio_level_t audio_level = AUDIO_MED;

/****************************************************************************
 *                              FUNCTION DECLARATIONS
 ***************************************************************************/
static void show_next_notification(void);

/*******************************************************************************
 *                              FUNCTION DEFINITIONS
 ******************************************************************************/

/*******************************************************************************
* Function Name: get_deg_delay_sec
********************************************************************************
* Summary:
*  Get delay (in seconds) per degree of temperature change based on thermostat
*  mode.
*
* Parameters:
*  mode - Thermostat mode (e.g., MODE_ECO, MODE_RAPID, MODE_AUTO).
*
* Return:
*  int - Delay in seconds per degree. Returns -1 for unsupported modes.
*
*******************************************************************************/
static int get_deg_delay_sec(thermostat_mode_t mode)
{
    switch (mode)
    {
        case MODE_ECO:
            return ECO_MODE_DEG_DELAY_SEC;

        case MODE_RAPID:
            return RAPID_MODE_DEG_DELAY_SEC;

        case MODE_AUTO:
            return AUTO_MODE_DEG_DELAY_SEC;

        default:
            return -1;
    }
}

/*******************************************************************************
* Function Name: calculate_remaining_time_sec
********************************************************************************
* Summary:
*  Calculate the total estimated time (in seconds) required to reach the set
*  temperature.
*
* Parameters:
*  mode - Thermostat mode in which the delay is calculated.
*  current_temp - Current measured temperature.
*  set_temp - Desired set temperature.
*
* Return:
*  uint32_t - Estimated time in seconds. Returns 0 if mode delay is invalid.
*
*******************************************************************************/
static uint32_t calculate_remaining_time_sec(thermostat_mode_t mode, int current_temp, int set_temp)
{
    int delay_per_deg = get_deg_delay_sec(mode);

    if (delay_per_deg <= 0)
    {
        return 0;
    }

    int temp_diff = abs(set_temp - current_temp);
    return temp_diff * delay_per_deg;
}

/*******************************************************************************
* Function Name: generate_thermostat_time_str
********************************************************************************
* Summary:
*  Generate a human-readable string indicating time remaining to reach the set
*  temperature.
*
* Parameters:
*  mode - Thermostat operating mode.
*  current_temp - Current temperature.
*  set_temp - Target temperature to reach.
*  out_str - Output buffer for the message string.
*  len - Length of the output buffer.
*
* Return:
*  void
*
*******************************************************************************/
static void generate_thermostat_time_str(thermostat_mode_t mode, int current_temp, int set_temp, char *out_str,
        size_t len)
{
    static uint32_t update_time = 0;

    if (mode == MODE_OFF)
    {
        snprintf(out_str, len, "System is OFF");
    }
    else
    {
        int temp_diff = set_temp - current_temp;

        if (temp_diff == 0)
        {
            snprintf(out_str, len, "Temperature already at %d", set_temp);
        }
        else
        {
            uint32_t delay_sec = calculate_remaining_time_sec(mode, current_temp, set_temp);
            uint32_t delay_min = (delay_sec + 59) / 60;

            if (update_time != delay_min)
            {
                update_time = delay_min;
            }

            if (temp_diff < 0)
            {
                snprintf(out_str, len, "%u min until cooled to %d", (unsigned int) delay_min, set_temp);
            }
            else
            {
                snprintf(out_str, len, "%u min until heated to %d", (unsigned int) delay_min, set_temp);
            }

            dev_info.thermostat_settings.time_remains = delay_sec;
        }
    }
}


/*******************************************************************************
* Function Name: set_volume
********************************************************************************
* Summary:
*  Sets the system speaker volume based on the specified audio level.
*
* Parameters:
*  level - The desired audio level to be applied.
*
* Return:
*  void
*
*******************************************************************************/
static void set_volume(audio_level_t level)
{
    /* Update speaker volume based on the received audio level */
    switch (level)
    {
        case AUDIO_OFF:
            app_speaker_audio_lvl_ctrl(AUDIO_LVL_OFF);
            break;

        case AUDIO_LOW:
            app_speaker_audio_lvl_ctrl(AUDIO_LVL_LOW);
            break;

        case AUDIO_MED:
            app_speaker_audio_lvl_ctrl(AUDIO_LVL_MED);
            break;

        case AUDIO_HIGH:
            app_speaker_audio_lvl_ctrl(AUDIO_LVL_HIGH);
            break;
    }

    audio_level = level;
    current_settings.audio.level = level;

    /* Store updated settings in NVM */
    update_current_device_setting();
}

/*******************************************************************************
* Function Name: fan_off
********************************************************************************
* Summary:
*  Turns the fan off and updates the UI accordingly.
*
* Parameters:
*  void
*
* Return:
*  void
*
*******************************************************************************/
static void fan_off(void)
{
    /* Update fan mode to off */
    dev_fan_mode = FAN_OFF;

    /* Delete fan mode animation */
    lv_anim_del(ui_fanoff, NULL);
    lv_anim_del(ui_fanactivehigh, NULL);
    lv_anim_del(ui_fanactivelow, NULL);
    lv_anim_del(ui_fanactivemed, NULL);

    /* Hide all the images initially */
    lv_obj_add_flag(ui_fanoff, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ui_fanactivehigh, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ui_fanactivelow, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ui_fanactivemed, LV_OBJ_FLAG_HIDDEN);

    /* Clear the fan off image and start animation */
    lv_obj_clear_flag(ui_fanoff, LV_OBJ_FLAG_HIDDEN);
    fanspeed_Animation(ui_fanoff, 0);
}

/*******************************************************************************
* Function Name: fan_high
********************************************************************************
* Summary:
*  Sets the fan to high speed and updates the UI.
*
* Parameters:
*  void
*
* Return:
*  void
*
*******************************************************************************/
static void fan_high(void)
{
    /* Update fan mode to high */
    dev_fan_mode = FAN_HIGH;

    /* Delete fan mode animation */
    lv_anim_del(ui_fanoff, NULL);
    lv_anim_del(ui_fanactivehigh, NULL);
    lv_anim_del(ui_fanactivelow, NULL);
    lv_anim_del(ui_fanactivemed, NULL);

    /* Hide all the images initially */
    lv_obj_add_flag(ui_fanoff, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ui_fanactivehigh, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ui_fanactivelow, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ui_fanactivemed, LV_OBJ_FLAG_HIDDEN);

    /* Clear the fan high image and start animation */
    lv_obj_clear_flag(ui_fanactivehigh, LV_OBJ_FLAG_HIDDEN);
    fanspeedhigh_Animation(ui_fanactivehigh, 0);
}

/*******************************************************************************
* Function Name: fan_med
********************************************************************************
* Summary:
*  Sets the fan to medium speed and updates the UI.
*
* Parameters:
*  void
*
* Return:
*  void
*
*******************************************************************************/
static void fan_med(void)
{
    /* Update fan mode to medium */
    dev_fan_mode = FAN_MED;

    /* Delete fan mode animation */
    lv_anim_del(ui_fanoff, NULL);
    lv_anim_del(ui_fanactivehigh, NULL);
    lv_anim_del(ui_fanactivelow, NULL);
    lv_anim_del(ui_fanactivemed, NULL);

    /* Hide all the images initially */
    lv_obj_add_flag(ui_fanoff, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ui_fanactivehigh, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ui_fanactivelow, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ui_fanactivemed, LV_OBJ_FLAG_HIDDEN);

    /* Clear the fan medium image and start animation */
    lv_obj_clear_flag(ui_fanactivemed, LV_OBJ_FLAG_HIDDEN);
    fanspeedmed_Animation(ui_fanactivemed, 0);
}

/*******************************************************************************
* Function Name: fan_low
********************************************************************************
* Summary:
*  Sets the fan to low speed and updates the UI.
*
* Parameters:
*  void
*
* Return:
*  void
*
*******************************************************************************/
static void fan_low(void)
{
    /* Update fan mode to low */
    dev_fan_mode = FAN_LOW;

    /* Delete fan mode animation */
    lv_anim_del(ui_fanoff, NULL);
    lv_anim_del(ui_fanactivehigh, NULL);
    lv_anim_del(ui_fanactivelow, NULL);
    lv_anim_del(ui_fanactivemed, NULL);

    /* Hide all the images initially */
    lv_obj_add_flag(ui_fanoff, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ui_fanactivehigh, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ui_fanactivelow, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ui_fanactivemed, LV_OBJ_FLAG_HIDDEN);

    /* Clear the fan low image and start animation */
    lv_obj_clear_flag(ui_fanactivelow, LV_OBJ_FLAG_HIDDEN);
    fanspeedlow_Animation(ui_fanactivelow, 0);
}

/*******************************************************************************
* Function Name: hide_notification_ready_cb
********************************************************************************
* Summary:
*  Callback executed when the hide animation completes. Hides the notification
*  UI element and displays the next notification in the queue.
*
* Parameters:
*  a - Pointer to the completed animation object.
*
* Return:
*  void
*
*******************************************************************************/
static void hide_notification_ready_cb(lv_anim_t *a)
{
    lv_obj_add_flag(ui_notification, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ui_notificationsetting, LV_OBJ_FLAG_HIDDEN);

    /* Clear old message (optional) */
    memset(&notif_queue[notif_head], 0, sizeof(notification_data_t));
    
    /* Move to next message */
    notif_head = (notif_head + 1) % MAX_NOTIF_QUEUE;
    notif_showing = false;

    show_next_notification();
}

/*******************************************************************************
* Function Name: hide_notification
********************************************************************************
* Summary:
*  Initiates the animation to hide the current notification.
*
* Parameters:
*  timer - Pointer to the LVGL timer that triggered this function.
*
* Return:
*  void
*
*******************************************************************************/
static void hide_notification(lv_timer_t *timer)
{
    lv_obj_t *current_screen = lv_scr_act();
    if(current_screen != ui_ActiveScreen){
        lv_anim_t a;
        lv_anim_init(&a);
        lv_anim_set_var(&a, ui_notificationsetting);
        lv_anim_set_values(&a, -197, -297);
        lv_anim_set_time(&a, 300);
        lv_anim_set_exec_cb(&a, (lv_anim_exec_xcb_t) lv_obj_set_y);
        lv_anim_start(&a);
        notif_showing = true;
    }

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, ui_notification);
    lv_anim_set_values(&a, -197, -297);
    lv_anim_set_time(&a, 300);
    lv_anim_set_exec_cb(&a, (lv_anim_exec_xcb_t) lv_obj_set_y);
    lv_anim_set_ready_cb(&a, hide_notification_ready_cb);
    lv_anim_start(&a);

    lv_timer_del(timer);
    notif_timer = NULL;
}

/*******************************************************************************
* Function Name: is_notification_duplicate
********************************************************************************
* Summary:
*  Checks whether a given notification already exists in the queue.
*
* Parameters:
*  type - Type of the notification.
*  status - Status associated with the notification.
*  value - Optional value for additional context.
*
* Return:
*  bool - true if a duplicate is found, false otherwise.
*
*******************************************************************************/
static bool is_notification_duplicate(notification_type type, notification_status_t status, uint32_t value)
{
    int i = notif_head;
    while (i != notif_tail)
    {
        if (notif_queue[i].type == type && notif_queue[i].status == status && notif_queue[i].value == value)
        {
            return true;
        }
        i = (i + 1) % MAX_NOTIF_QUEUE;
    }
    return false;
}

/*******************************************************************************
* Function Name: enqueue_notification
********************************************************************************
* Summary:
*  Adds a new notification to the queue if it is not a duplicate.
*
* Parameters:
*  type - Type of the notification.
*  status - Status of the notification.
*  value - Associated data value.
*
* Return:
*  void
*
*******************************************************************************/
void enqueue_notification(notification_type type, notification_status_t status, uint32_t value)
{
    if(bootup_done)
    {
    if (is_notification_duplicate(type, status, value))
        return;

    int next_tail = (notif_tail + 1) % MAX_NOTIF_QUEUE;
    if (next_tail == notif_head)
    {
        /* Queue full, drop notification */
        return;
    }

    notif_queue[notif_tail].type = type;
    notif_queue[notif_tail].status = status;
    notif_queue[notif_tail].value = value;
    notif_tail = next_tail;

    if (!notif_showing)
    {
        show_next_notification();
    }
}
}

/*******************************************************************************
* Function Name: show_next_notification
********************************************************************************
* Summary:
*  Displays the next notification from the queue.
*
* Parameters:
*  void
*
* Return:
*  void
*
*******************************************************************************/
static void show_next_notification(void)
{
    if (notif_head == notif_tail)
    {
        /* Queue empty */
        notif_showing = false;
        return;
    }

    lv_obj_t *current_screen = lv_scr_act();
    if(current_screen != ui_ActiveScreen && current_screen != ui_SettingsScreen) lv_scr_load(ui_ActiveScreen);
    
    notification_data_t *n = &notif_queue[notif_head];
    update_notifcation_label(n->type, n->status, n->value);

    lv_obj_clear_flag(ui_notification, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(ui_notificationsetting, LV_OBJ_FLAG_HIDDEN);

    if(current_screen != ui_ActiveScreen){
        lv_anim_t a;
        lv_anim_init(&a);
        lv_anim_set_var(&a, ui_notificationsetting);
        lv_anim_set_values(&a, -297, -197);
        lv_anim_set_time(&a, 300);
        lv_anim_set_exec_cb(&a, (lv_anim_exec_xcb_t) lv_obj_set_y);
        lv_anim_start(&a);
        notif_showing = true;
    }
    
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, ui_notification);
    lv_anim_set_values(&a, -297, -197);
    lv_anim_set_time(&a, 300);
    lv_anim_set_exec_cb(&a, (lv_anim_exec_xcb_t) lv_obj_set_y);
    lv_anim_start(&a);

    notif_showing = true;

    if (notif_timer)
    {
        lv_timer_del(notif_timer);
    }
    notif_timer = lv_timer_create(hide_notification, NOTIFICATION_TIMEOUT_MS, NULL);

    /* Play notification chime based on notification type */
    app_speaker_play(notify_audio_type);
}

/*******************************************************************************
* Function Name: update_mode_label
********************************************************************************
* Summary:
*  Updates the mode label on the Active screen UI.
*
* Parameters:
*  mode - The current thermostat mode to display.
*
* Return:
*  void
*
*******************************************************************************/
static void update_mode_label(thermostat_mode_t mode)
{
    /* Update mode label on Active screen UI */
    switch (mode)
    {
        case MODE_ECO:
            lv_label_set_text_fmt(ui_MainModeLabel, "ECO");
            lv_label_set_text_fmt(ui_LPModelabel, "ECO");
            break;

        case MODE_RAPID:
            lv_label_set_text_fmt(ui_MainModeLabel, "RAPID");
            lv_label_set_text_fmt(ui_LPModelabel, "RAPID");
            break;

        case MODE_AUTO:
            lv_label_set_text_fmt(ui_MainModeLabel, "AUTO");
            lv_label_set_text_fmt(ui_LPModelabel, "AUTO");
            break;

        case MODE_OFF:
            lv_label_set_text_fmt(ui_MainModeLabel, "OFF");
            lv_label_set_text_fmt(ui_LPModelabel, "OFF");
            break;
        default:
            break;
    }
}

/*******************************************************************************
* Function Name: clip_angle
********************************************************************************
* Summary:
*  Clips the angle value within arc min/max bounds.
*
* Parameters:
*  angle - Angle value to clip.
*
* Return:
*  int - Clipped angle value.
*
*******************************************************************************/
static inline int clip_angle(int angle){
    int ARC_ANGLE_MAX = 403;
    int ARC_ANGLE_MIN = 137;

    if (angle > ARC_ANGLE_MAX ) return ARC_ANGLE_MAX;
    if (angle < ARC_ANGLE_MIN ) return ARC_ANGLE_MIN;
    return angle;
}

/*******************************************************************************
* Function Name: temp_to_heating_arc_angle
********************************************************************************
* Summary:
*  Converts a temperature value to a heating arc angle.
*
* Parameters:
*  temperature - Temperature value.
*
* Return:
*  int - Arc angle value.
*
*******************************************************************************/
static int temp_to_heating_arc_angle(int temperature)
{
    if(dev_unit==TEMP_UNIT_FAHRENHEIT){
        int temp_min, temp_max, angle_per_Step;
        temp_min = TEMP_MIN*9/5 + 32;
        temp_max = TEMP_MAX*9/5 + 32;
        angle_per_Step = 300/(temp_max-temp_min);
        if (temperature < temp_min) temperature = temp_min;
        if (temperature > temp_max) temperature = temp_max;
        angle = TEMPERATURE_ARC_START_ANGLE + (temperature - temp_min) * angle_per_Step;
        return clip_angle(angle);
    }
    else{
        if (temperature < TEMP_MIN) temperature = TEMP_MIN;
        if (temperature > TEMP_MAX) temperature = TEMP_MAX;
        angle = TEMPERATURE_ARC_START_ANGLE + (temperature - TEMP_MIN) * TEMPERATURE_ARC_ANGLE_PER_STEP;
        return clip_angle(angle);
    }
}

/*******************************************************************************
* Function Name: temp_to_cooling_arc_angle
********************************************************************************
* Summary:
*  Converts a temperature value to a cooling arc angle.
*
* Parameters:
*  temperature - Temperature value.
*
* Return:
*  int - Arc angle value.
*
*******************************************************************************/
static int temp_to_cooling_arc_angle(int temperature)
{
    if(dev_unit==TEMP_UNIT_FAHRENHEIT){
        int temp_min, temp_max, angle_per_Step;
        temp_min = TEMP_MIN*9/5 + 32;
        temp_max = TEMP_MAX*9/5 + 32;
        angle_per_Step = 300/(temp_max-temp_min);
        if (temperature < temp_min) temperature = temp_min;
        if (temperature > temp_max) temperature = temp_max;
        angle = TEMPERATURE_ARC_END_ANGLE - (temp_max - temperature) * angle_per_Step;
        return clip_angle(angle);
    }
    else{
        if (temperature < TEMP_MIN) temperature = TEMP_MIN;
        if (temperature > TEMP_MAX) temperature = TEMP_MAX;
        angle = TEMPERATURE_ARC_END_ANGLE - ((TEMP_MAX - temperature) * TEMPERATURE_ARC_ANGLE_PER_STEP);
        return clip_angle(angle);
    }
}

/*******************************************************************************
* Function Name: increase_temp_step
********************************************************************************
* Summary:
*  Timer callback to incrementally increase current temperature towards target.
*
* Parameters:
*  timer - Pointer to the LVGL timer.
*
* Return:
*  void
*
*******************************************************************************/
static void increase_temp_step(lv_timer_t *timer)
{
    if(bootup_done)
    {
    if (dev_current_mode != MODE_OFF)
    {
        if (current_temp < target_temp)
        {
            current_temp++;
            lv_label_set_text_fmt(ui_TemperatureCurrValueLbl, "%d", current_temp);
            lv_label_set_text_fmt(ui_MainTemptextLP, "%d", current_temp);

            lv_label_set_text_fmt(ui_MainModeLabel, ". . . Heating . . .");
            lv_label_set_text_fmt(ui_LPModelabel, ". . . Heating . . .");

            generate_thermostat_time_str(dev_current_mode, current_temp, target_temp, subinfo, sizeof(subinfo));
            current_state = STATE_HEATING;


            lv_obj_remove_flag(ui_ArcTempIndicator, LV_OBJ_FLAG_HIDDEN);
            lv_arc_set_value(ui_ArcTempIndicator, 35); 

            int start_angle = temp_to_heating_arc_angle(current_temp)+1;
            int end_angle   = temp_to_heating_arc_angle(target_temp);

            printf("Updated Start angle: %d, End Angle: %d\n", start_angle, end_angle);
            printf("Updated CT: %d, TT: %d\n", current_temp, target_temp);

            lv_arc_set_bg_start_angle(ui_ArcTempIndicator, start_angle);
            lv_arc_set_bg_end_angle(ui_ArcTempIndicator, end_angle);

        }
        if (current_temp == target_temp)
        {
            /** Switch to Active screen  */
            switch_to_active_screen();

            lv_timer_del(timer);
            temp_timer = NULL;
            lv_label_set_text_fmt(ui_TemperatureCurrValueLbl, "%d", current_temp);
            lv_label_set_text_fmt(ui_MainTemptextLP, "%d", current_temp);

            lv_arc_set_value(ui_ArcTempControl, current_temp);

            lv_obj_add_flag(ui_currenttemp, LV_OBJ_FLAG_HIDDEN);
            update_mode_label(dev_current_mode);
            enqueue_notification(NOTIFY_TEMP_UPDATE, NOTIF_SUCCESS, current_temp);
            dev_info.thermostat_settings.time_remains = 0;
            current_state = STATE_NONE;

            lv_obj_add_flag(ui_ArcTempIndicator, LV_OBJ_FLAG_HIDDEN);
        }

        dev_info.environment.current_temp = current_temp;

        /* Update sensor data if device is connected */
        if (true == is_device_connected)
        {
            update_temperature_data_ipc(&dev_info);
        }
    }
}
}

/*******************************************************************************
* Function Name: decrease_temp_step
********************************************************************************
* Summary:
*  Timer callback to incrementally decrease current temperature towards target.
*
* Parameters:
*  timer - Pointer to the LVGL timer.
*
* Return:
*  void
*
*******************************************************************************/
static void decrease_temp_step(lv_timer_t *timer)
{
    if(bootup_done)
    {
    if (dev_current_mode != MODE_OFF)
    {
        lv_obj_remove_flag(ui_currenttemp, LV_OBJ_FLAG_HIDDEN);
        if (current_temp > target_temp)
        {
            current_temp--;
            lv_label_set_text_fmt(ui_MainModeLabel, ". . . Cooling . . .");
            lv_label_set_text_fmt(ui_LPModelabel, ". . . Cooling . . .");

            lv_label_set_text_fmt(ui_TemperatureCurrValueLbl, "%d", current_temp);
            lv_label_set_text_fmt(ui_MainTemptextLP, "%d", current_temp);

            generate_thermostat_time_str(dev_current_mode, current_temp, target_temp, subinfo, sizeof(subinfo));
            current_state = STATE_COOLING;

            lv_obj_remove_flag(ui_ArcTempIndicator, LV_OBJ_FLAG_HIDDEN);
            lv_arc_set_value(ui_ArcTempIndicator, 15); 

            int start_angle = temp_to_cooling_arc_angle(current_temp);
            int end_angle   = temp_to_cooling_arc_angle(target_temp);

            printf("Start angle: %d, End Angle: %d\n", start_angle, end_angle);
            printf("CT: %d, TT: %d\n", current_temp, target_temp);

            lv_arc_set_bg_start_angle(ui_ArcTempIndicator, end_angle);
            lv_arc_set_bg_end_angle(ui_ArcTempIndicator, start_angle);

        }
        if (current_temp == target_temp)
        {
            /** Switch to Active screen  */
            switch_to_active_screen();

            lv_timer_del(timer);
            temp_timer = NULL;
            lv_label_set_text_fmt(ui_TemperatureCurrValueLbl, "%d", current_temp);
            lv_label_set_text_fmt(ui_MainTemptextLP, "%d", current_temp);

            lv_arc_set_value(ui_ArcTempControl, current_temp);
            lv_obj_add_flag(ui_currenttemp, LV_OBJ_FLAG_HIDDEN);
            update_mode_label(dev_current_mode);
            enqueue_notification(NOTIFY_TEMP_UPDATE, NOTIF_SUCCESS, current_temp);
            dev_info.thermostat_settings.time_remains = 0;
            current_state = STATE_NONE;

            lv_obj_add_flag(ui_ArcTempIndicator, LV_OBJ_FLAG_HIDDEN);
        }

        dev_info.environment.current_temp = current_temp;

        /* Update sensor data if device is connected */
        if (true == is_device_connected)
        {
            update_temperature_data_ipc(&dev_info);
        }
    }
}
}

/*******************************************************************************
* Function Name: increase_temp
********************************************************************************
* Summary:
*  Handles the increase temperature button event.
*
* Parameters:
*  e - Pointer to the LVGL event.
*
* Return:
*  void
*
*******************************************************************************/
void increase_temp(lv_event_t *e)
{
    if (dev_current_mode != MODE_OFF)
    {
        if (temperature < current_max_temp)
        {
            lv_obj_add_flag(ui_ArcTempIndicator, LV_OBJ_FLAG_HIDDEN);
            lv_arc_set_value(ui_ArcTempControl, 35);
            target_temp = ++temperature;
            if (dev_unit == TEMP_UNIT_CELSIUS)
            {
                lv_label_set_text_fmt(ui_currenttemp, "%d°C", temperature);
            }
            else
            {
                lv_label_set_text_fmt(ui_currenttemp, "%d°F", temperature);
            }
            lv_obj_set_style_text_color(ui_currenttemp, lv_color_hex(0xF44336), LV_PART_MAIN | LV_STATE_DEFAULT);
            if (popup_overlay_visible == false)
            {
                lv_obj_remove_flag(ui_currenttemp, LV_OBJ_FLAG_HIDDEN);
            }

            current_state = STATE_HEATING;

            if (temp_timer)
            {
                lv_timer_del(temp_timer);
                temp_timer = NULL;
            }
            temp_timer = lv_timer_create(increase_temp_step, deg2sec, NULL);
            lv_label_set_text_fmt(ui_MainModeLabel, ". . . Heating . . .");
            lv_label_set_text_fmt(ui_LPModelabel, ". . . Heating . . .");

            dev_info.environment.target_temp = target_temp;
            /* Update sensor data if device is connected */
            if (true == is_device_connected)
            {
                update_temperature_data_ipc(&dev_info);
            }
            generate_thermostat_time_str(dev_current_mode, current_temp, target_temp, subinfo, sizeof(subinfo));

            lv_obj_remove_flag(ui_ArcTempIndicator, LV_OBJ_FLAG_HIDDEN);
            lv_arc_set_value(ui_ArcTempIndicator, 35); 
        }
    }
    else
    {
        LOG_INFO(CYLF_DEF, "Increase temperature error. Device in Off mode.\n");
    }
}

/*******************************************************************************
* Function Name: decrease_temp
********************************************************************************
* Summary:
*  Handles the decrease temperature button event.
*
* Parameters:
*  e - Pointer to the LVGL event.
*
* Return:
*  void
*
*******************************************************************************/
void decrease_temp(lv_event_t *e)
{
    if (dev_current_mode != MODE_OFF)
    {
        if (temperature > current_min_temp)
        {
            lv_obj_add_flag(ui_ArcTempIndicator, LV_OBJ_FLAG_HIDDEN);
            lv_arc_set_value(ui_ArcTempControl, 15);
            target_temp = --temperature;

            if (dev_unit == TEMP_UNIT_CELSIUS)
            {
                lv_label_set_text_fmt(ui_currenttemp, "%d°C", temperature);
            }
            else
            {
                lv_label_set_text_fmt(ui_currenttemp, "%d°F", temperature);
            }

            lv_obj_set_style_text_color(ui_currenttemp, lv_color_hex(0xC6FFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
            if (popup_overlay_visible == false)
            {
                lv_obj_remove_flag(ui_currenttemp, LV_OBJ_FLAG_HIDDEN);
            }
            current_state = STATE_COOLING;
            if (temp_timer)
            {
                lv_timer_del(temp_timer);
                temp_timer = NULL;
            }
            temp_timer = lv_timer_create(decrease_temp_step, deg2sec, NULL);
            lv_label_set_text_fmt(ui_MainModeLabel, ". . . Cooling . . .");
            lv_label_set_text_fmt(ui_LPModelabel, ". . . Cooling . . .");
            dev_info.environment.target_temp = target_temp;
            /* Update sensor data if device is connected */
            if (true == is_device_connected)
            {
                update_temperature_data_ipc(&dev_info);
            }
            generate_thermostat_time_str(dev_current_mode, current_temp, target_temp, subinfo, sizeof(subinfo));

            lv_obj_remove_flag(ui_ArcTempIndicator, LV_OBJ_FLAG_HIDDEN);
            lv_arc_set_value(ui_ArcTempIndicator, 15); 
        }
        else
        {
            LOG_INFO(CYLF_DEF, "Thermostat Minimum temperature reached.\n");
        }
    }
    else
    {
        LOG_INFO(CYLF_DEF, "Decrease temperature error. Device in Off mode.\n");
    }
}

/*******************************************************************************
* Function Name: update_fan_mode
********************************************************************************
* Summary:
*  Updates the fan mode, sets animation, label, and sends IPC update.
*
* Parameters:
*  mode - Fan speed mode to set.
*
* Return:
*  void
*
*******************************************************************************/
void update_fan_mode(fan_speed_t mode)
{
    /* Set Active screen fan animation and helper label
     * based on selected mode */
    switch (mode)
    {
        case FAN_HIGH:
            fan_high();
            lv_label_set_text_fmt(ui_fanmodelabel, "HIGH");
            break;
        case FAN_LOW:
            fan_low();
            lv_label_set_text_fmt(ui_fanmodelabel, "LOW");
            break;
        case FAN_MED:
            fan_med();
            lv_label_set_text_fmt(ui_fanmodelabel, "MED");
            break;
        case FAN_OFF:
            fan_off();
            lv_label_set_text_fmt(ui_fanmodelabel, "OFF");
            current_state = STATE_NONE;
            break;

        default:
            fan_off();
            lv_label_set_text_fmt(ui_fanmodelabel, "OFF");
            current_state = STATE_NONE;
            break;
    }

    dev_fan_mode = mode;

    /* Update info on MApp or Cloud if connected */
    if (true == is_device_connected)
    {
        /* Update fan mode info. to CM33 core using IPC */
        update_fan_speed_ipc(dev_fan_mode);
    }
    if(current_settings.isTutorial){
        stop_fan_anim();
    }
}

/*******************************************************************************
* Function Name: fan_clicked
********************************************************************************
* Summary:
*  Handles fan button click event. Cycles through available fan modes.
*
* Parameters:
*  e - Pointer to the LVGL event.
*
* Return:
*  void
*
*******************************************************************************/
void fan_clicked(lv_event_t *e)
{
    CY_UNUSED_PARAMETER(e);

    if (dev_current_mode != MODE_OFF)
    {
        /* Device is ON → fan cannot be OFF */
        if (dev_fan_mode == FAN_OFF || dev_fan_mode == FAN_HIGH)
        {
            dev_fan_mode = FAN_LOW; /* Start cycle at LOW */
        }
        else if (dev_fan_mode == FAN_LOW)
        {
            dev_fan_mode = FAN_MED;
        }
        else if (dev_fan_mode == FAN_MED)
        {
            dev_fan_mode = FAN_HIGH;
        }
    }
    else
    {
        /* Device is OFF → allow full cycle including FAN_OFF */
        if (dev_fan_mode == FAN_LOW)
        {
            dev_fan_mode = FAN_OFF;
        }
        else
        {
            dev_fan_mode++;
        }
    }

    update_fan_mode(dev_fan_mode);
    current_settings.thermostat_setting.fan_mode = dev_fan_mode;

    /* Save updated setting */
    update_current_device_setting();
    if(current_settings.isTutorial) {
        enqueue_notification(NOTIFY_FAN_TUTORIAL, NOTIF_FAIL, 0);
    }
}


/*******************************************************************************
* Function Name: update_display_brightness
********************************************************************************
* Summary:
*  Updates the display brightness level.
*
* Parameters:
*  level - Brightness level (0-100).
*
* Return:
*  void
*
*******************************************************************************/
void update_display_brightness(uint8_t level)
{
    if (level<30) level = 30;
    lv_slider_set_value(ui_BrightnessSlider, level, LV_ANIM_OFF);
    mtb_display_st7701s_set_brightness(level);
    brightness_level = level;
    LOG_INFO(CYLF_DEF, "Brightness Level: %d\n", brightness_level);
    dev_info.preferences.display_brightness = level;
    current_settings.display_setting.brightness = level;
    update_current_device_setting();
}

/*******************************************************************************
* Function Name: change_brightness
********************************************************************************
* Summary:
*  Handles brightness slider change event.
*
* Parameters:
*  e - Pointer to the LVGL event.
*
* Return:
*  void
*
*******************************************************************************/
void change_brightness(lv_event_t *e)
{
    uint8_t slider_val = lv_slider_get_value(ui_BrightnessSlider);

    if ((slider_val % 10) > 5)
    {
        brightness_level = ((slider_val / 10) * 10) + 10;
    }
    else
    {
        brightness_level = ((slider_val / 10) * 10);
    }

    update_display_brightness(brightness_level);

    /* Is device connected to MApp or Cloud */
    if (true == is_device_connected)
    {
        update_brightness_ipc(brightness_level);
    }
}

/*******************************************************************************
* Function Name: update_weather_icon
********************************************************************************
* Summary:
*  Updates the weather icon on the UI.
*
* Parameters:
*  weather_icon - Weather type to display.
*
* Return:
*  void
*
*******************************************************************************/
void update_weather_icon(weather_t weather_icon)
{
    switch (weather_icon)
    {
    case CLEAR:
        lv_image_set_src(ui_weathericon, &ui_img_clear_png);
        break;
    case CLOUDY:
        lv_image_set_src(ui_weathericon, &ui_img_sun_png);
        break;
    case RAIN:
        lv_image_set_src(ui_weathericon, &ui_img_rain_png);
        break;
    case SNOW:
        lv_image_set_src(ui_weathericon, &ui_img_snowflake_png);
        break;
    case THUNDER:
        lv_image_set_src(ui_weathericon, &ui_img_thunder_png);
        break;
    case FOG:
        lv_image_set_src(ui_weathericon, &ui_img_fog_png);
        break;
    default:
        break;
    }
}

/*******************************************************************************
* Function Name: update_weather_info_via_http
********************************************************************************
* Summary:
*  Updates weather info on the UI using HTTP-fetched data.
*
* Parameters:
*  void
*
* Return:
*  void
*
*******************************************************************************/
void update_weather_info_via_http()
{
    char *end_ptr = NULL;
    long weather_code_long = strtol(weather_code_sync_value, &end_ptr, 10);

    if ((end_ptr == weather_code_sync_value) || (*end_ptr != '\0'))
    {
        LOG_INFO(CYLF_DEF, "Invalid weather code string\n");
        return;
    }

    int weatherCode = (int) weather_code_long;

    switch (weatherCode) {
        case 0:
            LOG_INFO(CYLF_DEF,"Clear Sky \n");
            update_weather_icon(CLEAR);
            break; /* Clear sky */
        case 1: case 2: case 3:
            LOG_INFO(CYLF_DEF,"Partly Cloudy \n");
            update_weather_icon(CLOUDY);
            break; /* Partly cloudy / Mainly Clear */
        case 45: case 48:
            LOG_INFO(CYLF_DEF,"Fog \n");
            update_weather_icon(FOG);
            break; /* Cloudy / Foggy */
        case 51: case 53: case 55: case 61: case 63: case 65: case 80: case 81: case 82:
            LOG_INFO(CYLF_DEF,"Rain \n");
            update_weather_icon(RAIN);
            break; /* Drizzle / Rain */
        case 56: case 57: case 66: case 67: case 71: case 73: case 75: case 77: case 85: case 86:
            LOG_INFO(CYLF_DEF,"Snowflake \n");
            update_weather_icon(SNOW);
            break; /* Freezing Rain */
        case 95: case 96: case 99:
            LOG_INFO(CYLF_DEF,"Rain With Thunder \n");
            update_weather_icon(THUNDER);
            break; /* Rain with Thunder */
        default:
            printf("UNKNOWN WEATHER CODE GROUP!!! \n");
            break; /* Unknown weather code */
    }
}


#if defined (RADAR_DETECTION)
/*******************************************************************************
* Function Name: show_presence_icon_and_update_label
********************************************************************************
* Summary:
*  Shows the presence icon and updates the label with person count.
*
* Parameters:
*  person_count - Number of persons detected.
*
* Return:
*  void
*
*******************************************************************************/
void show_presence_icon_and_update_label(uint8_t person_count)
{
    char buf[10];
    sprintf(buf, "x %d", person_count);

    /* Set full opacity for the presence icon, and unhide the
     * presence label if count is >1 */
     _ui_opacity_set(ui_presencelbl, 230);

}

/*******************************************************************************
* Function Name: update_presence_detection
********************************************************************************
* Summary:
*  Updates the presence detection state and UI.
*
* Parameters:
*  presence_count - Number of persons detected.
*
* Return:
*  void
*
*******************************************************************************/
void update_presence_detection(uint8_t presence_count)
{
    stop_active_state_timer();
    LOG_INFO(CYLF_DEF, "Presence : %d\n", presence_count);
    if (presence_count == 0)
    {
        person_detected = false;
        start_inactivity_timer_addn(3000); /* 3 seconds */
    }
    else 
    {
        person_detected = true;
        start_inactivity_timer();

    }
    display_presence_detection_status();
    return;
}

/*******************************************************************************
* Function Name: hide_presence_icon
********************************************************************************
* Summary:
*  Hides the presence detection icon.
*
* Parameters:
*  void
*
* Return:
*  void
*
*******************************************************************************/
void hide_presence_icon(void)
{
    lv_obj_set_style_opa(ui_presencelbl, 100, 0);
}

/*******************************************************************************
* Function Name: display_presence_detection_status
********************************************************************************
* Summary:
*  Displays the presence detection status on the UI.
*
* Parameters:
*  void
*
* Return:
*  void
*
*******************************************************************************/
void display_presence_detection_status(void)
{
    if (person_detected == true)
    {
        show_presence_icon_and_update_label(person_count);
         _ui_opacity_set(ui_presencelbl, 230);
    }
    else
    {
        hide_presence_icon();
    }
}

#endif
/*******************************************************************************
* Function Name: hide_popup_timer_cb
********************************************************************************
* Summary:
*  FreeRTOS timer callback. Sets flag to hide the connectivity screen.
*
* Parameters:
*  xTimer - Handle of the timer that expired.
*
* Return:
*  void
*
*******************************************************************************/
static void hide_popup_timer_cb(lv_timer_t *timer)
{
    hide_conn_screen = true;
    popup_auto_hide_pending = false;
}

/*******************************************************************************
* Function Name: hide_connectivity_screen
********************************************************************************
* Summary:
*  Hides the connectivity popup overlay when the device is connected.
*
* Parameters:
*  void
*
* Return:
*  void
*
*******************************************************************************/
void hide_connectivity_screen(void)
{
    if((dev_current_conn_state == DEV_ST_BLE_CONNECTED) ||
            (dev_current_conn_state == DEV_ST_WIFI_CONNECTED) ||
            (dev_current_conn_state == DEV_ST_CLOUD_CONNECTED)||
            (dev_current_conn_state == DEV_ST_CLOUD_DISCONNECTED)
        )
    {
        lv_obj_add_flag(ui_popupoverlay, LV_OBJ_FLAG_HIDDEN);
        wifi_popup_state = false;
        popup_overlay_visible = false;

        /* Start fan mode animation running in background */
        display_fan_anim();
        display_temp_change_anim();

        _ui_flag_modify(ui_ArcGroup, LV_OBJ_FLAG_HIDDEN, _UI_MODIFY_FLAG_REMOVE);
    }
}

/***********************switch_to_active_screen********************************************************
* Function Name: ui_timer_init
********************************************************************************
* Summary:
*  Initialize the UI one-shot timer.
*
* Parameters:
*  void
*
* Return:
*  void
*
*******************************************************************************/
void ui_timer_init(void)
{
    /* Create a one-shot LVGL timer of 2000ms, paused initially */
    lvgl_timer = lv_timer_create(hide_popup_timer_cb, 2000, NULL);

    if(lvgl_timer == NULL)
    {
        LOG_INFO(CYLF_DEF, "ui_timer_init error.\n");
        return;
    }

    lv_timer_set_repeat_count(lvgl_timer, 1);
    lv_timer_pause(lvgl_timer);
}

/*******************************************************************************
* Function Name: ui_timer_stop
********************************************************************************
* Summary:
*  Stop the UI timer if it is running.
*
* Parameters:
*  void
*
* Return:
*  void
*
*******************************************************************************/
void ui_timer_stop(void)
{
    if (lvgl_timer != NULL) {
        lv_timer_pause(lvgl_timer);
        popup_auto_hide_pending = false;
    }
}

/*******************************************************************************
* Function Name: ui_timer_start
********************************************************************************
* Summary:
*  Start the UI timer.
*
* Parameters:
*  void
*
* Return:
*  void
*
*******************************************************************************/
void ui_timer_start(void)
{
    if (lvgl_timer != NULL) {
        lv_timer_set_repeat_count(lvgl_timer, 1);
        lv_timer_reset(lvgl_timer);
        lv_timer_resume(lvgl_timer);
        popup_auto_hide_pending = true;
    }
}

/*******************************************************************************
* Function Name: update_device_connection_state
********************************************************************************
* Summary:
*  Updates the device connection state and corresponding UI elements.
*
* Parameters:
*  state - The current device connection state.
*
* Return:
*  void
*
*******************************************************************************/
void update_device_connection_state(device_connection_state_t state)
{
    const char *state_text = "Unknown";

    /* Hide all hidden widgets (images, button, labels) initially */
    lv_obj_add_flag(ui_commissionMapp, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ui_commissionKeyboard, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ui_autoreconnectpanel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ui_connstatekeyboardbtn, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ui_devconnstatecontianer, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ui_bleconmappinfolabel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ui_qrcodecontainer, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(ui_devconnstatecontianer, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ui_cloudconnected, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ui_wificonnecting120, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ui_clouddisconn120, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ui_bleadv, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ui_devstateimg, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ui_wificonnectedimg, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ui_wifidisabledimg, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ui_bleconnected120, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ui_wifi, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ui_wifistatic, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ui_homebleconnected, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ui_bleconnected50, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ui_homewifidisconnimg, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ui_homewifidisconnimg, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ui_homecloudconnimg, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ui_qrcodebtn, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ui_wifideletebtn, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ui_qrcodebtn, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ui_cloudconnecting, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ui_connectionprg1, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ui_connectionprg2, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ui_connectionprg3, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ui_switchtowifilbl, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ui_bleswitchbtn, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ui_mappinfolabel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ui_devicestatelabel, LV_OBJ_FLAG_HIDDEN);

    /* Delete all running animations */
    lv_anim_del(ui_bleadv, NULL);
    lv_anim_del(ui_wificonnecting120, NULL);
    lv_anim_del(ui_wifi, NULL);
    lv_anim_del(ui_homebleconnected, NULL);
    lv_anim_del(ui_connectionprg1, NULL);
    lv_anim_del(ui_connectionprg2, NULL);
    lv_anim_del(ui_connectionprg3, NULL);


    /* Update UI based on received device state */
    switch (state)
    {
        case DEV_ST_CLOUD_CONNECTED:

            /* Update connection state in cm55 core */
            is_device_connected = true;

            /* Update connection state on popup screen */
            lv_obj_add_flag(ui_progressCancelBtn, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(ui_connstatekeyboardbtn, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(ui_autoreconnectpanel, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(ui_deviceIDlabel, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(ui_cloudconnected, LV_OBJ_FLAG_HIDDEN);
            state_text = "Cloud Connected";
            lv_obj_clear_flag(ui_qrcodebtn, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(ui_switchtoKeybd, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(ui_wifideletebtn, LV_OBJ_FLAG_HIDDEN);

            /* Update icon on home screen */
            lv_obj_clear_flag(ui_homecloudconnimg, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(ui_bleswitchbtn, LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_style_opa(ui_homecloudconnimg, 255, 0);

            /* Add Cloud connected status in notification queue */
            enqueue_notification(NOTIFY_NETWORK_STATUS, NOTIF_SUCCESS, DEV_ST_CLOUD_CONNECTED);

            /* Start timer to hide the pop-up screen */
            ui_timer_stop();
            ui_timer_start();
            ble_conn_state = false;
            cloud_conn_state = true;
            break;

        case DEV_ST_CLOUD_CONNECTING:

            /* Update connection state on popup screen */
            lv_obj_add_flag(ui_progressCancelBtn, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(ui_connstatekeyboardbtn, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(ui_progressCancelBtn, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(ui_connstatekeyboardbtn, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(ui_devconnstatecontianer, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(ui_cloudconnecting, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(ui_progressCancelBtn, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(ui_autoreconnectpanel, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(ui_wifideletebtn, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(ui_switchtoKeybd, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(ui_deviceIDlabel, LV_OBJ_FLAG_HIDDEN);
            if(cloud_reconnect_retry == CLOUD_CONN_RETRY_LIMITED)
            {
                lv_obj_clear_state(ui_Switch2, LV_STATE_CHECKED);
            }
            else
            {
                lv_obj_add_state(ui_Switch2, LV_STATE_CHECKED);
            }

            state_text = "Cloud Connecting . . .";

            /* Update icon on home screen */
            lv_obj_clear_flag(ui_homewifidisconnimg, LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_style_opa(ui_homewifidisconnimg, 255, 0);
            cloud_conn_state = false;
            break;

        case DEV_ST_WIFI_CONNECTING:

            /* Update connection state on popup screen */
            lv_obj_add_flag(ui_progressCancelBtn, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(ui_connstatekeyboardbtn, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(ui_devconnstatecontianer, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(ui_wificonnecting120, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(ui_connectionprg1, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(ui_connectionprg2, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(ui_connectionprg3, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(ui_progressCancelBtn, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(ui_autoreconnectpanel, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(ui_wifideletebtn, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(ui_switchtoKeybd, LV_OBJ_FLAG_HIDDEN);
            progress1_Animation(ui_connectionprg1, 0);
            progress2_Animation(ui_connectionprg2, 0);
            progress3_Animation(ui_connectionprg3, 0);
            if(wifi_reconnect_retry == WIFI_CONN_RETRY_LIMITED)
            {
                lv_obj_clear_state(ui_Switch2, LV_STATE_CHECKED);
            }
            else
            {
                lv_obj_add_state(ui_Switch2, LV_STATE_CHECKED);
            }
            state_text = "Wi-Fi Connecting . . .";

            /* Update icon on home screen */
            lv_obj_clear_flag(ui_wifi, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(ui_deviceIDlabel, LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_style_opa(ui_wifi, 255, 0);
            wifi_Animation(ui_wifi, 0);
            wifi_conn_state = false;
            cloud_conn_state = false;
            break;

        case DEV_ST_CLOUD_DISCONNECTED:
            /* Update connection state in cm55 core */
            is_device_connected = false;

            /* Update connection state on popup screen */
            lv_obj_add_flag(ui_progressCancelBtn, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(ui_connstatekeyboardbtn, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(ui_devconnstatecontianer, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(ui_clouddisconn120, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(ui_wifideletebtn, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(ui_autoreconnectpanel, LV_OBJ_FLAG_HIDDEN);
            if(wifi_reconnect_retry == WIFI_CONN_RETRY_LIMITED)
            {
                lv_obj_clear_state(ui_Switch2, LV_STATE_CHECKED);
            }
            else
            {
                lv_obj_add_state(ui_Switch2, LV_STATE_CHECKED);
            }
            state_text = "Cloud Disconnected";

            /* Update icon on home screen */
            lv_obj_clear_flag(ui_homewifidisconnimg, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(ui_bleswitchbtn, LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_style_opa(ui_homewifidisconnimg, 255, 0);

            /* Start timer to hide the pop-up screen */
            ui_timer_stop();
            ui_timer_start();

            /* Add Cloud disconnected status in notification queue */
            enqueue_notification(NOTIFY_NETWORK_STATUS, NOTIF_FAIL, DEV_ST_CLOUD_DISCONNECTED);
            cloud_conn_state = false;
            break;

        case DEV_ST_BLE_ADVERTISING:

            /* Update connection state on popup screen */
            lv_obj_add_flag(ui_progressCancelBtn, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(ui_connstatekeyboardbtn, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(ui_devicestateimg, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(ui_switchtoKeybd, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(ui_mappinfobutton, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(ui_mappinfolabel, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(ui_devconnstatecontianer, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(ui_bleadv, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(ui_progressCancelBtn, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(ui_autoreconnectpanel, LV_OBJ_FLAG_HIDDEN);
            if(ble_reconnect_retry == BLE_CONN_RETRY_LIMITED)
            {
                lv_obj_clear_state(ui_Switch2, LV_STATE_CHECKED);
            }
            else
            {
                lv_obj_add_state(ui_Switch2, LV_STATE_CHECKED);
            }
            /* Only show the switch to WiFi option if device is provisioned  */
            if (true == is_device_provisioned)
            {
                lv_obj_clear_flag(ui_switchtowifilbl, LV_OBJ_FLAG_HIDDEN);
            }
            bleadvpulse_Animation(ui_bleadv, 0);
            if(ble_conn_state){update_device_connection_state(DEV_ST_BLE_CONNECTED); state_text = "BLE Connected"; break;}
            state_text = "Waiting for mobile connection . . .";
            lv_obj_clear_flag(ui_deviceIDlabel, LV_OBJ_FLAG_HIDDEN);
            /* Update icon on home screen */
            lv_obj_clear_flag(ui_homebleconnected, LV_OBJ_FLAG_HIDDEN);
            wifi_Animation(ui_homebleconnected, 0);
            lv_obj_set_style_opa(ui_homebleconnected, 255, 0);

            ble_conn_state = false;
            break;

        case DEV_ST_BLE_PAIRING:

            /* Update connection state on popup screen */
            lv_obj_add_flag(ui_progressCancelBtn, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(ui_connstatekeyboardbtn, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(ui_commissionMapp, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(ui_commissionKeyboard, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(ui_devconnstatecontianer, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(ui_qrcodecontainer, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(ui_popupoverlay, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(ui_devconnstatecontianer, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(ui_popupoverlay, LV_OBJ_FLAG_HIDDEN);

            /* Update icon on home screen */
            lv_obj_clear_flag(ui_homebleconnected, LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_style_opa(ui_homebleconnected, 255, 0);
            
            break;

        case DEV_ST_UNPROVISIONED:
            /* Update connection state in cm55 core */
            is_device_connected = false;

            /* Update connection state on popup screen */
            lv_obj_add_flag(ui_progressCancelBtn, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(ui_connstatekeyboardbtn, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(ui_devconnstatecontianer, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(ui_qrcodecontainer, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(ui_commissionKeyboard, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(ui_commissionMapp, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(ui_devicestateimg, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(ui_switchtoKeybd, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(ui_mappinfobutton, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(ui_mappinfolabel, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(ui_devicestatelabel, LV_OBJ_FLAG_HIDDEN);
            
            state_text = "Wi-Fi not connected\nTap to start mobile pair";

            /* Update icon on home screen */
            lv_obj_add_flag(ui_wifi, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(ui_wifistatic, LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_style_opa(ui_wifistatic, 130, LV_PART_MAIN | LV_STATE_DEFAULT);
            wifi_conn_state = false;
            ble_conn_state = false;
            cloud_conn_state = false;
            break;

        case DEV_ST_WIFI_CONNECTED:

            /* Update connection state on popup screen */
            lv_obj_add_flag(ui_progressCancelBtn, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(ui_connstatekeyboardbtn, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(ui_devconnstatecontianer, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(ui_wificonnectedimg, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(ui_wifideletebtn, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(ui_bleswitchbtn, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(ui_deviceIDlabel, LV_OBJ_FLAG_HIDDEN);
            state_text = "Wi-Fi Connected";

            /* Update icon on home screen */
            lv_obj_clear_flag(ui_homewifidisconnimg, LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_style_opa(ui_homewifidisconnimg, 255, 0);

            /* Add Wi-Fi connected status in notification queue */
            enqueue_notification(NOTIFY_NETWORK_STATUS, NOTIF_SUCCESS, DEV_ST_WIFI_CONNECTED);
            wifi_conn_state = true;

            /* Start timer to hide the pop-up screen */
            ui_timer_stop();
            ui_timer_start();
            break;

        case DEV_ST_WIFI_DISCONNECTED:

            /* Update connection state on popup screen */
            
            lv_obj_add_flag(ui_progressCancelBtn, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(ui_connstatekeyboardbtn, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(ui_devconnstatecontianer, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(ui_wifidisabledimg, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(ui_wifideletebtn, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(ui_switchtoKeybd, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(ui_autoreconnectpanel, LV_OBJ_FLAG_HIDDEN);
            if(wifi_reconnect_retry == WIFI_CONN_RETRY_LIMITED)
            {
                lv_obj_clear_state(ui_Switch2, LV_STATE_CHECKED);
            }
            else
            {
                lv_obj_add_state(ui_Switch2, LV_STATE_CHECKED);
            }
            state_text = "Wi-Fi Disconnected";

            /* Update icon on home screen */
            lv_obj_clear_flag(ui_homewifidisconnimg, LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_style_opa(ui_homewifidisconnimg, 255, 0);

            /* Add Wi-Fi disconnected status in notification queue */
            enqueue_notification(NOTIFY_NETWORK_STATUS, NOTIF_FAIL, DEV_ST_WIFI_DISCONNECTED);
            wifi_conn_state = false;
            break;

        case DEV_ST_BLE_CONNECTED:
            /* Update connection state in cm55 core */
            is_device_connected = true;
            
            /* Update connection state on popup screen */
            lv_obj_add_flag(ui_progressCancelBtn, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(ui_connstatekeyboardbtn, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(ui_devconnstatecontianer, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(ui_bleconnected120, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(ui_mappinfobutton, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(ui_bleconmappinfolabel, LV_OBJ_FLAG_HIDDEN);

            /* Only show the switch to WiFi option if device is provisioned  */
            if ( true == is_device_provisioned || wifi_conn_state)
            {
                lv_obj_clear_flag(ui_switchtowifilbl, LV_OBJ_FLAG_HIDDEN);
                lv_obj_add_flag(ui_connstatekeyboardbtn, LV_OBJ_FLAG_HIDDEN);
                lv_obj_add_flag(ui_commissionMapp, LV_OBJ_FLAG_HIDDEN);
                lv_obj_add_flag(ui_bleconmappinfolabel, LV_OBJ_FLAG_HIDDEN);
            }
            else
            {
                lv_obj_clear_flag(ui_mappinfolabel, LV_OBJ_FLAG_HIDDEN);
            }

            state_text = "BLE Connected";

            /* Update icon on home screen */
            lv_obj_clear_flag(ui_bleconnected50, LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_style_opa(ui_bleconnected50, 190, 0);

            /* Start timer to hide the pop-up screen */
            ui_timer_stop();
            ui_timer_start();
            ble_conn_state = false;
            break;

        default:
            break;
    }

    /* Update device state label in popup screen */
    lv_label_set_text(ui_devstatelabel, state_text);
    
    /* Update device current connection state */
    dev_current_conn_state = state;
    dev_last_conn_state = state;
}

/*******************************************************************************
* Function Name: start_ble_adv
********************************************************************************
* Summary:
*  Starts BLE advertising for provisioning.
*
* Parameters:
*  e - Pointer to the LVGL event.
*
* Return:
*  void
*
*******************************************************************************/
void start_ble_adv(lv_event_t *e)
{
    CY_UNUSED_PARAMETER(e);
    LOG_INFO(CYLF_DEF, "CM55: Start BLE ADV\r\n");

    prov_method = PROV_MAPP_BLE;

    /* Send start BLE ADV CMD to CM33 core using IPC */
    update_ble_adv_ipc(DEV_ST_BLE_ADVERTISING);
}

/*******************************************************************************
* Function Name: connect_wifi
********************************************************************************
* Summary:
*  Starts the Wi-Fi connection process using UI-provided credentials.
*
* Parameters:
*  e - Pointer to the LVGL event.
*
* Return:
*  void
*
*******************************************************************************/
void connect_wifi(lv_event_t *e)
{
    CY_UNUSED_PARAMETER(e);

    /* Get the SSID and Password from the text field */
    const char *ssid = lv_textarea_get_text(ui_ssidfield);
    const char *password = lv_textarea_get_text(ui_passwordfield);

    prov_method = PROV_UI_KEYBOARD;

    /* Update UI with Wi-Fi connecting status */
    update_device_connection_state(DEV_ST_WIFI_CONNECTING);

    /* Send start WiFi connection along with Wi-Fi credentials
    to CM33 core using IPC */
    update_wifi_cred_ipc(ssid, password);
}

/*******************************************************************************
* Function Name: update_wifi_cred
********************************************************************************
* Summary:
*  Event handler to update Wi-Fi credentials.
*
* Parameters:
*  e - Pointer to the LVGL event.
*
* Return:
*  void
*
*******************************************************************************/
void update_wifi_cred(lv_event_t *e)
{
    CY_UNUSED_PARAMETER(e);

    /* Get the SSID and Password from the text field */
    const char *ssid = lv_textarea_get_text(ui_devstatecontainerssidfield);
    const char *password = lv_textarea_get_text(ui_devstatecontainerpasswordfield);
    
    /* Update UI with Wi-Fi connecting status */
    update_device_connection_state(DEV_ST_WIFI_CONNECTING);

    /* Send start WiFi connection along with Wi-Fi credentials
    to CM33 core using IPC */
    update_wifi_cred_ipc(ssid, password);
}

/*******************************************************************************
* Function Name: update_notifcation_label
********************************************************************************
* Summary:
*  Updates the notification label and image based on the operation result.
*
* Parameters:
*  type - The type of notification.
*  status - The status of the operation.
*  value - The value associated with the operation result.
*
* Return:
*  void
*
*******************************************************************************/
void update_notifcation_label(notification_type type, notification_status_t status, uint32_t value)
{
    if(bootup_done)
    {
    if (NOTIF_FAIL == status)
    {
        /* Set notification audio  */
        notify_audio_type = AUDIO_ERROR;

        /* Set image in notification label */
        lv_img_set_src(ui_notificationlblimg, &ui_img_incorrect_img_png);
        lv_img_set_src(ui_notificationlblimgsetting, &ui_img_incorrect_img_png);


        /* Set text in notification label */
        switch (type)
        {
            case NOTIFY_NETWORK_STATUS:
                switch ((device_connection_state_t) value)
                {
                    case DEV_ST_CLOUD_DISCONNECTED:
                        lv_label_set_text(ui_notificationlabel, "Disconnected from Cloud.");
                        lv_label_set_text(ui_notificationlabelsetting, "Disconnected from Cloud.");
                        break;

                    case DEV_ST_WIFI_DISCONNECTED:
                        lv_label_set_text(ui_notificationlabel, "Disconnected from WiFi network.");
                        lv_label_set_text(ui_notificationlabelsetting, "Disconnected from WiFi network.");
                        break;

                    case DEV_ST_NO_INTERNET:
                        lv_label_set_text(ui_notificationlabel, "Oops! No Internet, Try again.");
                        lv_label_set_text(ui_notificationlabelsetting, "Oops! No Internet, Try again.");
                        break;

                    default:
                        break;
                }
                break;

            case NOTIFY_TEMP_UPDATE:
                if (dev_unit == TEMP_UNIT_CELSIUS)
                {
                    lv_label_set_text_fmt(ui_notificationlabel, "%u°C, Temp out of range or same!", value);
                    lv_label_set_text_fmt(ui_notificationlabelsetting, "%u°C, Temp out of range or same!", value);

                }
                else
                {
                    lv_label_set_text_fmt(ui_notificationlabel, "Command is currently not supported ");
                    lv_label_set_text_fmt(ui_notificationlabelsetting, "Command is currently not supported");

                }
                break;

            case NOTIFY_MODE_UPDATE:
                lv_label_set_text(ui_notificationlabel, "Failed to set mode.");
                lv_label_set_text(ui_notificationlabelsetting, "Failed to set mode.");
                break;

            case NOTIFY_FAN_MODE_UPDATE:
                lv_label_set_text(ui_notificationlabel, "Failed to set fan mode.");
                lv_label_set_text(ui_notificationlabelsetting, "Failed to set fan mode.");
                break;

            case NOTIFY_FW_UPDATE:
                lv_label_set_text(ui_notificationlabel, "Latest firmware installed, no update.");
                lv_label_set_text(ui_notificationlabelsetting, "Latest firmware installed, no update.");
                break;

            case NOTIFY_HTTP_SYNC:
                lv_label_set_text(ui_notificationlabel, "Failed to sync Location, Weather, Timezone");
                lv_label_set_text(ui_notificationlabelsetting, "Failed to sync Location, Weather, Timezone");
                break;

            case NOTIFY_HUM_WARNING:
                lv_label_set_text(ui_notificationlabel, "High Humidity Alert !!!");
                lv_label_set_text(ui_notificationlabelsetting, "High Humidity Alert !!!");
                break;
            
            case NOTIFY_CONNECTIVITY_TUTORIAL:
                lv_label_set_text(ui_notificationlabel, "Finish tutorial to access connectivity");
                break;

            case NOTIFY_FAN_TUTORIAL:
                lv_label_set_text(ui_notificationlabel, "Finish tutorial to change fan mode");
                break;
            
            default:
                break;
        }
    }
    else if (NOTIF_SUCCESS == status)
    {
        /* Set notification audio  */
        notify_audio_type = AUDIO_SUCCESS;

        /* Set image in notification label */
        lv_img_set_src(ui_notificationlblimg, &ui_img_correct_img_png);
        lv_img_set_src(ui_notificationlblimgsetting, &ui_img_correct_img_png);


        /* Set text in notification label */
        switch (type)
        {
            case NOTIFY_NETWORK_STATUS:
                switch ((device_connection_state_t) value)
                {
                    case DEV_ST_CLOUD_CONNECTED:
                        lv_label_set_text(ui_notificationlabel, "Connected to Cloud.");
                        lv_label_set_text(ui_notificationlabelsetting, "Connected to Cloud.");
                        break;

                    case DEV_ST_WIFI_CONNECTED:
                        lv_label_set_text(ui_notificationlabel, "Connected to WiFi network.");
                        lv_label_set_text(ui_notificationlabelsetting, "Connected to WiFi network.");
                        break;

                    default:
                        break;
                }
                break;
            case NOTIFY_TEMP_UPDATE:
                if (dev_unit == TEMP_UNIT_CELSIUS)
                {
                    lv_label_set_text_fmt(ui_notificationlabel, "Temperature set-point reached %u°C.", value);
                    lv_label_set_text_fmt(ui_notificationlabelsetting, "Temperature set-point reached %u°C.", value);
                }
                else
                {
                    lv_label_set_text_fmt(ui_notificationlabel, "Temperature set-point reached %u°F.", value);
                    lv_label_set_text_fmt(ui_notificationlabelsetting, "Temperature set-point reached %u°F.", value);
                }

                break;
            case NOTIFY_MODE_UPDATE:
                lv_label_set_text_fmt(ui_notificationlabel, "Mode set to %u.", value); 
                lv_label_set_text_fmt(ui_notificationlabelsetting, "Mode set to %u.", value);
                break;
            case NOTIFY_FAN_MODE_UPDATE:
                lv_label_set_text_fmt(ui_notificationlabel, "Fan mode set to %u.", value); 
                lv_label_set_text_fmt(ui_notificationlabelsetting, "Fan mode set to %u.", value); 
                break;

            case NOTIFY_HTTP_SYNC:
                lv_label_set_text(ui_notificationlabel, "Synced Location, Weather, Timezone");
                lv_label_set_text(ui_notificationlabelsetting, "Synced Location, Weather, Timezone");
                break;

            case NOTIFY_HUM_VENTI:
                lv_label_set_text(ui_notificationlabel, "Improving ventilation . . . ");
                lv_label_set_text(ui_notificationlabelsetting, "Improving ventilation . . . ");
                break;

            case NOTIFY_HUM_VENTI_IMP:
                lv_label_set_text(ui_notificationlabel, "Humidity Improved !!!");
                lv_label_set_text(ui_notificationlabelsetting, "Humidity Improved !!!");
                break;

            default:
                break;
        }
    }
}
}

/*******************************************************************************
* Function Name: hide_mappinfolabel
********************************************************************************
* Summary:
*  Hides the mobile app info label after timeout.
*
* Parameters:
*  timer - Pointer to the LVGL timer.
*
* Return:
*  void
*
*******************************************************************************/
void hide_mappinfolabel(lv_timer_t *timer)
{
    lv_obj_add_flag(ui_mappinfolabel, LV_OBJ_FLAG_HIDDEN);
}

/*******************************************************************************
* Function Name: process_mappinfobutton_ex
********************************************************************************
* Summary:
*  Button tap event handler for mobile app info button.
*
* Parameters:
*  e - Pointer to the LVGL event.
*
* Return:
*  void
*
*******************************************************************************/
void process_mappinfobutton_ex(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_CLICKED)
    {
        /* Show the label */
        lv_obj_clear_flag(ui_mappinfolabel, LV_OBJ_FLAG_HIDDEN);

        /* Start a 2-second (2000 ms) timer to hide the label */
        lv_timer_create(hide_mappinfolabel, 2000, NULL);
    }
}

/*******************************************************************************
* Function Name: hide_keyboardinfolabel
********************************************************************************
* Summary:
*  Hides the keyboard info label after timeout.
*
* Parameters:
*  timer - Pointer to the LVGL timer.
*
* Return:
*  void
*
*******************************************************************************/
void hide_keyboardinfolabel(lv_timer_t *timer)
{
    lv_obj_add_flag(ui_keyboardinfolabel, LV_OBJ_FLAG_HIDDEN);
}

/*******************************************************************************
* Function Name: process_keyboardinfobutton_ex
********************************************************************************
* Summary:
*  Button tap event handler for keyboard info button.
*
* Parameters:
*  e - Pointer to the LVGL event.
*
* Return:
*  void
*
*******************************************************************************/
void process_keyboardinfobutton_ex(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_CLICKED)
    {
        /* Show the label */
        lv_obj_clear_flag(ui_keyboardinfolabel, LV_OBJ_FLAG_HIDDEN);

        /* Start a 2-second (2000 ms) timer to hide the label */
        lv_timer_create(hide_mappinfolabel, 2000, NULL);
    }
}

/*******************************************************************************
* Function Name: wifi_ilabel_timer_callback
********************************************************************************
* Summary:
*  Timer callback function for Wi-Fi info label.
*
* Parameters:
*  xTimer - Handle of the timer that expired.
*
* Return:
*  void
*
*******************************************************************************/
static void wifi_ilabel_timer_callback(TimerHandle_t xTimer)
{

}

/*******************************************************************************
* Function Name: start_wifi_settings_ilabel_timer_ex
********************************************************************************
* Summary:
*  Starts the Wi-Fi settings info label timer.
*
* Parameters:
*  void
*
* Return:
*  void
*
*******************************************************************************/
void start_wifi_settings_ilabel_timer_ex(void)
{

    if (wifi_ilabel_timer == NULL)
    {
        /* Create the timer (auto-reload = pdFALSE for one-shot) */
        wifi_ilabel_timer = xTimerCreate("WifiILabelTimer", pdMS_TO_TICKS(WIFI_ILABEL_TIMER_TIMEOUT_MS), pdFALSE,
        NULL, wifi_ilabel_timer_callback);
    }

    if (wifi_ilabel_timer != NULL)
    {
        /* If already active, stop it first */
        if (xTimerIsTimerActive(wifi_ilabel_timer))
        {
            xTimerStop(wifi_ilabel_timer, 0);
        }

        /* Start (or restart) the timer */
        xTimerStart(wifi_ilabel_timer, 0);
    }
}
/*******************************************************************************
* Function Name: stop_wifi_settings_ilabel_timer_ex
********************************************************************************
* Summary:
*  Stops the Wi-Fi settings info label timer.
*
* Parameters:
*  void
*
* Return:
*  void
*
*******************************************************************************/
void stop_wifi_settings_ilabel_timer_ex(void)
{
    if (wifi_ilabel_timer != NULL)
    {
        if (xTimerIsTimerActive(wifi_ilabel_timer))
        {
            xTimerStop(wifi_ilabel_timer, 0);
        }
    }
}

/*******************************************************************************
* Function Name: wifi_kybd_label_timer_callback
********************************************************************************
* Summary:
*  Timer callback function for Wi-Fi keyboard label.
*
* Parameters:
*  xTimer - Handle of the timer that expired.
*
* Return:
*  void
*
*******************************************************************************/
static void wifi_kybd_label_timer_callback(TimerHandle_t xTimer)
{
    if (!lv_obj_has_flag(ui_keyboardinfolabel, LV_OBJ_FLAG_HIDDEN))
    {
        lv_obj_add_flag(ui_keyboardinfolabel, LV_OBJ_FLAG_HIDDEN);
    }
}

/*******************************************************************************
* Function Name: fw_update_check_cb
********************************************************************************
* Summary:
*  Timer callback to set the firmware update timeout flag.
*
* Parameters:
*  xTimer - Handle of the timer that expired.
*
* Return:
*  void
*
*******************************************************************************/
static void fw_update_check_cb(TimerHandle_t xTimer)
{    
    fw_update_timeout = true;
}

/*******************************************************************************
* Function Name: start_wifi_settings_kybd_label_timer_ex
********************************************************************************
* Summary:
*  Starts the Wi-Fi settings keyboard label timer.
*
* Parameters:
*  void
*
* Return:
*  void
*
*******************************************************************************/
void start_wifi_settings_kybd_label_timer_ex(void)
{
    if (lv_obj_has_flag(ui_keyboardinfolabel, LV_OBJ_FLAG_HIDDEN))
    {
        lv_obj_clear_flag(ui_keyboardinfolabel, LV_OBJ_FLAG_HIDDEN);
    }
    if (wifi_kybd_label_timer == NULL)
    {
        /* Create the timer (auto-reload = pdFALSE for one-shot) */
        wifi_kybd_label_timer = xTimerCreate("WifiKybdLabelTimer", pdMS_TO_TICKS(WIFI_ILABEL_TIMER_TIMEOUT_MS), pdFALSE,
        NULL, wifi_kybd_label_timer_callback);
    }

    if (wifi_kybd_label_timer != NULL)
    {
        /* If already active, stop it first */
        if (xTimerIsTimerActive(wifi_kybd_label_timer))
        {
            xTimerStop(wifi_kybd_label_timer, 0);
        }

        /* Start (or restart) the timer */
        xTimerStart(wifi_kybd_label_timer, 0);
    }
}
/*******************************************************************************
* Function Name: stop_wifi_settings_kybd_label_timer_ex
********************************************************************************
* Summary:
*  Stops the Wi-Fi settings keyboard label timer.
*
* Parameters:
*  void
*
* Return:
*  void
*
*******************************************************************************/
void stop_wifi_settings_kybd_label_timer_ex(void)
{
    if (wifi_ilabel_timer != NULL)
    {
        if (xTimerIsTimerActive(wifi_kybd_label_timer))
        {
            xTimerStop(wifi_kybd_label_timer, 0);
        }
    }
}

/*******************************************************************************
* Function Name: set_thermostat_mode
********************************************************************************
* Summary:
*  Updates the thermostat mode and reflects changes in the UI and system state.
*
* Parameters:
*  mode - The thermostat mode to set.
*
* Return:
*  void
*
*******************************************************************************/
void set_thermostat_mode(thermostat_mode_t mode)
{
    /* Update image and labels based on current mode */
    switch (mode)
    {
        case MODE_ECO:
            lv_obj_set_style_bg_image_src(ui_ModeButton, &ui_img_eco_png, LV_PART_MAIN | LV_STATE_DEFAULT);
            deg2sec = ECO_MODE_TEMP_TIMER_TIMEOUT;
            update_fan_mode(FAN_LOW);
            LOG_INFO(CYLF_DEF, "Mode set to ECO\n");
            break;

        case MODE_RAPID:
            lv_obj_set_style_bg_image_src(ui_ModeButton, &ui_img_mode_select_rapid_png, LV_PART_MAIN | LV_STATE_DEFAULT);
            deg2sec = RAPID_MODE_TEMP_TIMER_TIMEOUT;
            update_fan_mode(FAN_HIGH);
            LOG_INFO(CYLF_DEF, "Mode set to RAPID\n");
            break;

        case MODE_AUTO:
            lv_obj_set_style_bg_image_src(ui_ModeButton, &ui_img_automode_png_png, LV_PART_MAIN | LV_STATE_DEFAULT);
            deg2sec = AUTO_MODE_TEMP_TIMER_TIMEOUT;
            update_fan_mode(FAN_MED);
            LOG_INFO(CYLF_DEF, "Mode set to AUTO\n");
            if (dev_unit == TEMP_UNIT_CELSIUS)
            {
                update_device_temp(AUTO_MODE_TARGET_TEMP_DEFAULT_C);
            }
            else
            {
                update_device_temp(AUTO_MODE_TARGET_TEMP_DEFAULT_F);
            }
            break;

        case MODE_OFF:
            lv_obj_set_style_bg_image_src(ui_ModeButton, &ui_img_mode_select_fan_png, LV_PART_MAIN | LV_STATE_DEFAULT);
            update_fan_mode(FAN_OFF);
            lv_obj_add_flag(ui_ArcTempIndicator, LV_OBJ_FLAG_HIDDEN);
            LOG_INFO(CYLF_DEF, "Mode set to FAN\n");

            /* If temperature increase decrease timer running, stop it */
            if (temp_timer)
            {
                lv_timer_del(temp_timer);
                temp_timer = NULL;
            }

            target_temp = current_temp;
            temperature = current_temp;
            lv_obj_add_flag(ui_currenttemp, LV_OBJ_FLAG_HIDDEN);
            lv_arc_set_value(ui_ArcTempControl, current_temp);
            break;

        default:
            break;
    }

    /* Update mode label on Active screen  */
    update_mode_label(mode);

    /* Update info on MApp or Cloud if connected */
    if (true == is_device_connected)
    {
        /* Send mode info to CM33 core using IPC */
        update_device_mode_ipc(mode);
    }

    /* Store updated setting in NVM */
    update_thermostat_mode_timer();
}

/*******************************************************************************
* Function Name: toggle_mode
********************************************************************************
* Summary:
*  Handles thermostat mode toggle from the UI event.
*
* Parameters:
*  e - Pointer to the LVGL event.
*
* Return:
*  void
*
*******************************************************************************/
void toggle_mode(lv_event_t *e)
{
    CY_UNUSED_PARAMETER(e);

    /* Advance to next mode */
    dev_current_mode = (dev_current_mode + 1) % MODE_MAX;

    /* Update UI based on the selected mode */
    set_thermostat_mode(dev_current_mode);
    lv_obj_set_style_opa(ui_ModeButton, LV_OPA_COVER, 0);

    /* Update current settings accordingly */
    current_settings.thermostat_setting.mode = dev_current_mode;
    current_settings.thermostat_setting.fan_mode = dev_fan_mode;

    /* Store updated mode in NVM */
    update_current_device_setting();
}

/*******************************************************************************
* Function Name: open_notifcaiton_ex
********************************************************************************
* Summary:
*  Handles notification popup click.
*
* Parameters:
*  void
*
* Return:
*  void
*
*******************************************************************************/
void open_notifcaiton_ex(void)
{
    LOG_INFO(CYLF_DEF, "Notification popup clicked.\n");

}
/*******************************************************************************
* Function Name: delete_wifi_cred
********************************************************************************
* Summary:
*  Deletes Wi-Fi credentials and resets provisioned state.
*
* Parameters:
*  e - Pointer to the LVGL event.
*
* Return:
*  void
*
*******************************************************************************/
void delete_wifi_cred(lv_event_t *e)
{
    request_wifi_delete_ipc();

    /* Update device provisioned state */
    is_device_provisioned = false;
    
}

/*******************************************************************************
* Function Name: display_qrcode
********************************************************************************
* Summary:
*  Displays a QR code for the device unique ID.
*
* Parameters:
*  e - Pointer to the LVGL event.
*
* Return:
*  void
*
*******************************************************************************/
void display_qrcode(lv_event_t *e)
{
    qr_manager_init(&qr_manager, ui_qrcodecontainer, 120);
    qr_manager_update(&qr_manager, device_unique_id);
    lv_label_set_text(ui_qrcodelabel, "Scan this QR code using the mobile app.");
}

/*******************************************************************************
* Function Name: hide_qr
********************************************************************************
* Summary:
*  Hides the QR code display.
*
* Parameters:
*  e - Pointer to the LVGL event.
*
* Return:
*  void
*
*******************************************************************************/
void hide_qr(lv_event_t *e)
{
    qr_manager_deinit(&qr_manager);
}

/*******************************************************************************
* Function Name: update_thermostat_mode
********************************************************************************
* Summary:
*  Updates the thermostat mode and corresponding UI elements.
*
* Parameters:
*  mode - The new thermostat mode to be set.
*
* Return:
*  void
*
*******************************************************************************/
void update_thermostat_mode(thermostat_mode_t mode)
{
    /* Update mode and fan mode image along with functionality
     * based on current mode */
    switch (mode)
    {
        case MODE_ECO:
            lv_obj_set_style_bg_image_src(ui_ModeButton, &ui_img_eco_png, LV_PART_MAIN | LV_STATE_DEFAULT);
            deg2sec = ECO_MODE_TEMP_TIMER_TIMEOUT;
            update_fan_mode(FAN_LOW);
            LOG_INFO(CYLF_DEF, "Mode set to ECO\n");
            break;

        case MODE_RAPID:
            lv_obj_set_style_bg_image_src(ui_ModeButton, &ui_img_mode_select_rapid_png, LV_PART_MAIN | LV_STATE_DEFAULT);
            deg2sec = RAPID_MODE_TEMP_TIMER_TIMEOUT;
            update_fan_mode(FAN_HIGH);
            LOG_INFO(CYLF_DEF, "Mode set to RAPID\n");
            break;

        case MODE_AUTO:
            lv_obj_set_style_bg_image_src(ui_ModeButton, &ui_img_automode_png_png, LV_PART_MAIN | LV_STATE_DEFAULT);
            deg2sec = AUTO_MODE_TEMP_TIMER_TIMEOUT;
            update_fan_mode(FAN_MED);
            LOG_INFO(CYLF_DEF, "Mode set to AUTO\n");
            if (dev_unit == TEMP_UNIT_CELSIUS)
            {
                update_device_temp(AUTO_MODE_TARGET_TEMP_DEFAULT_C);
            }
            else
            {
                update_device_temp(AUTO_MODE_TARGET_TEMP_DEFAULT_F);
            }
            break;

        case MODE_OFF:
            lv_obj_set_style_bg_image_src(ui_ModeButton, &ui_img_mode_select_fan_png, LV_PART_MAIN | LV_STATE_DEFAULT);
            update_fan_mode(FAN_OFF);
            LOG_INFO(CYLF_DEF, "Mode set to FAN\n");
            break;

        default:
            break;
    }

    /* Update current mode label on Active screen */
    update_mode_label(mode);
    dev_current_mode = mode;
}

/*******************************************************************************
* Function Name: is_fahrenheit_resolved
********************************************************************************
* Summary:
*  Checks if the temperature unit is Fahrenheit and resolves accordingly.
*
* Parameters:
*  void
*
* Return:
*  int - 1 if Celsius, 0 if Fahrenheit.
*
*******************************************************************************/
int is_fahrenheit_resolved(void)
{
    if (dev_unit == TEMP_UNIT_FAHRENHEIT){
        LOG_INFO(CYLF_DEF, "Command is currently not supported ");
        enqueue_notification(NOTIFY_TEMP_UPDATE, NOTIF_FAIL, NULL);
        return 0;
    }
    else{
        return 1;
    }

}

/*******************************************************************************
* Function Name: update_device_temp
********************************************************************************
* Summary:
*  Updates the target temperature and modifies the UI accordingly.
*
* Parameters:
*  temp - The new target temperature to set.
*
* Return:
*  void
*
*******************************************************************************/
void update_device_temp(uint8_t temp)
{
    if((temp < current_min_temp) || (temp > current_max_temp))
    {
        LOG_INFO(CYLF_DEF, "Temp is not in range or same as current temp!");
        enqueue_notification(NOTIFY_TEMP_UPDATE, NOTIF_FAIL, temp);
        return;
    }

    /* Update temperature only if thermostat is not in Off mode */
    if (dev_current_mode != MODE_OFF)
    {
        /* Udpate the target temperature */
        target_temp = temp;
        temperature = target_temp;
        dev_info.environment.target_temp = target_temp;

        /* Update temperature value in temperature Arc */
        lv_arc_set_value(ui_ArcTempControl, target_temp);

        /* Update label based on the current temperature unit */
        if (dev_unit == TEMP_UNIT_CELSIUS)
        {
            lv_label_set_text_fmt(ui_currenttemp, "%d°C", temperature);

        }
        else
        {
            lv_label_set_text_fmt(ui_currenttemp, "%d°F", temperature);
        }

        /* Delete temperature timer */
        if (temp_timer)
        {
            lv_timer_del(temp_timer);
            temp_timer = NULL;
        }

        /* If target temperature is greater then current temp., start
         * timer to increase the current temperature and display the
         * red arrows along with animation and heating status on UI. */
        if (target_temp > current_temp)
        {
            lv_obj_add_flag(ui_ArcTempIndicator, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(ui_ArcTempIndicator, LV_OBJ_FLAG_HIDDEN);

            lv_obj_set_style_text_color(ui_currenttemp, lv_color_hex(0xF44336), LV_PART_MAIN | LV_STATE_DEFAULT);
            temp_timer = lv_timer_create(increase_temp_step, deg2sec, NULL);
            if (popup_overlay_visible == false)
            {
                lv_obj_remove_flag(ui_currenttemp, LV_OBJ_FLAG_HIDDEN);
            }

            current_state = STATE_HEATING;

            /* Update UI with heating label */
            lv_label_set_text_fmt(ui_MainModeLabel, ". . . Heating . . .");

            /* Update UI with time remaining info to reach target temperature */
            generate_thermostat_time_str(dev_current_mode, current_temp, target_temp, subinfo, sizeof(subinfo));
 
            lv_obj_remove_flag(ui_ArcTempIndicator, LV_OBJ_FLAG_HIDDEN);
            lv_arc_set_value(ui_ArcTempIndicator, 35);
            
        
            int start_angle = temp_to_heating_arc_angle(current_temp)+1;
            int end_angle   = temp_to_heating_arc_angle(target_temp);
            if(start_angle>end_angle) end_angle = start_angle + 1;

            printf("Start angle: %d, End Angle: %d\n", start_angle, end_angle);
            printf("CT: %d, TT: %d\n", current_temp, target_temp);

            lv_arc_set_bg_start_angle(ui_ArcTempIndicator, start_angle);
            lv_arc_set_bg_end_angle(ui_ArcTempIndicator, end_angle);

        }
        /* If target temperature is less then current temp., start
         * timer to decrease the current temperature and display the
         * blue arrows with animation along with the cooling status on UI. */
        else if (target_temp < current_temp)
        {

            lv_obj_set_style_text_color(ui_currenttemp, lv_color_hex(0xC6FFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
            if (popup_overlay_visible == false)
            {
                lv_obj_remove_flag(ui_currenttemp, LV_OBJ_FLAG_HIDDEN);
            }
            current_state = STATE_COOLING;
            temp_timer = lv_timer_create(decrease_temp_step, deg2sec, NULL);

            /* Update UI with cooling label */
            lv_label_set_text_fmt(ui_MainModeLabel, ". . . Cooling . . .");

            /* Update UI with time remaining info to reach target temperature */
            generate_thermostat_time_str(dev_current_mode, current_temp, target_temp, subinfo, sizeof(subinfo));
  
            lv_obj_remove_flag(ui_ArcTempIndicator, LV_OBJ_FLAG_HIDDEN);
            lv_arc_set_value(ui_ArcTempIndicator, 15);

            int end_angle = temp_to_cooling_arc_angle(current_temp);
            int start_angle   = temp_to_cooling_arc_angle(target_temp);

            printf("Start angle: %d, End Angle: %d\n", start_angle, end_angle);
            printf("CT: %d, TT: %d\n", current_temp, target_temp);
            lv_arc_set_bg_start_angle(ui_ArcTempIndicator, start_angle);
            lv_arc_set_bg_end_angle(ui_ArcTempIndicator, end_angle);
        }
        /* If target temperature is set to current temp., stop all timer
         * and hide all red/blue arrow animations from UI. */
        else
        {
            lv_obj_add_flag(ui_ArcTempIndicator, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(ui_ArcTempIndicator, LV_OBJ_FLAG_HIDDEN);

            current_state = STATE_NONE;

            /* If timer running delete the instance */
            if (temp_timer != NULL)
            {
                lv_timer_del(temp_timer);
                temp_timer = NULL;
            }

            /* Hide all the Heating/Cooling labels */
            lv_obj_add_flag(ui_currenttemp, LV_OBJ_FLAG_HIDDEN);

            /* Update UI with current mode label */
            update_mode_label(dev_current_mode);
        }

        dev_info.environment.target_temp = target_temp;
        dev_info.environment.current_temp = current_temp;

    }
    else
    {
        LOG_INFO(CYLF_DEF, "Increase temperature error. Device in Off mode.\n");
    }

}


/*******************************************************************************
* Function Name: get_current_fan_mode
********************************************************************************
* Summary:
*  Gets the current fan mode.
*
* Parameters:
*  void
*
* Return:
*  fan_speed_t - Current fan mode.
*
*******************************************************************************/
fan_speed_t get_current_fan_mode(void)
{
    return dev_fan_mode;
}

/*******************************************************************************
* Function Name: get_current_device_mode
********************************************************************************
* Summary:
*  Gets the current thermostat mode.
*
* Parameters:
*  void
*
* Return:
*  thermostat_mode_t - Current thermostat mode.
*
*******************************************************************************/
thermostat_mode_t get_current_device_mode(void)
{
    return dev_current_mode;
}

/*******************************************************************************
* Function Name: get_current_brigthness
********************************************************************************
* Summary:
*  Gets the current display brightness level.
*
* Parameters:
*  void
*
* Return:
*  uint8_t - Brightness level (0-100).
*
*******************************************************************************/
uint8_t get_current_brigthness(void)
{
    return brightness_level;
}

/*******************************************************************************
* Function Name: get_current_temperature
********************************************************************************
* Summary:
*  Gets the current temperature.
*
* Parameters:
*  void
*
* Return:
*  int - Current temperature value.
*
*******************************************************************************/
int get_current_temperature(void)
{
    return current_temp;
}

/*******************************************************************************
* Function Name: load_thermostat_config
********************************************************************************
* Summary:
*  Loads and applies thermostat configuration to the UI.
*
* Parameters:
*  mode - Thermostat mode to be applied.
*
* Return:
*  void
*
*******************************************************************************/
void load_thermostat_config(thermostat_mode_t mode)
{
    /* Set Active screen UI components based on current
     * temperature unit. */
    if (current_settings.system.temperature_unit == TEMP_UNIT_FAHRENHEIT)
    {
        /* Update active screen UI components for degree Faranhiet unit */
        current_temp = CELSIUS_TO_FAHRENHEIT(current_temp);
        lv_obj_add_flag(ui_ArcTempIndicator,LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_state(ui_tempunitswitch, LV_STATE_CHECKED);
        dev_unit = TEMP_UNIT_FAHRENHEIT;
        lv_label_set_text_fmt(ui_TemperatureCurrValueLbl, "%d", current_temp);
        lv_label_set_text_fmt(ui_MainTemptextLP, "%d", current_temp);
        lv_label_set_text_fmt(ui_OutdoorTempLabel, "%d°F", CELSIUS_TO_FAHRENHEIT(atoi(weather_sync_value)));
        lv_arc_set_range(ui_ArcTempControl, TEMPERATURE_DEG_F_MIN_VALUE, TEMPERATURE_DEG_F_MAX_VALUE);
        lv_arc_set_value(ui_ArcTempControl, current_temp);
        current_max_temp = TEMPERATURE_DEG_F_MAX_VALUE;
        current_min_temp = TEMPERATURE_DEG_F_MIN_VALUE;
        dev_info.environment.current_temp = current_temp;

    }
    else
    {
        /* Update active screen UI components for degree Celcius unit */
        lv_obj_clear_state(ui_tempunitswitch, LV_STATE_CHECKED);
        dev_unit = TEMP_UNIT_CELSIUS;
        lv_label_set_text_fmt(ui_TemperatureCurrValueLbl, "%d", current_temp);
        lv_label_set_text_fmt(ui_MainTemptextLP, "%d", current_temp);
        lv_label_set_text_fmt(ui_OutdoorTempLabel, "%d°C", atoi(weather_sync_value));
        lv_arc_set_range(ui_ArcTempControl, TEMPERATURE_DEG_C_MIN_VALUE, TEMPERATURE_DEG_C_MAX_VALUE);
        lv_arc_set_value(ui_ArcTempControl, current_temp);
        current_max_temp = TEMPERATURE_DEG_C_MAX_VALUE;
        current_min_temp = TEMPERATURE_DEG_C_MIN_VALUE;
        dev_info.environment.current_temp = current_temp;    
    }

    /* Update UI based on the received thermostat mode */
    switch (mode)
    {
        case MODE_ECO: 
        case MODE_RAPID: 
        case MODE_AUTO: 
        case MODE_OFF:
            update_thermostat_mode(mode);
            lv_arc_set_value(ui_ArcTempControl, current_temp);
            target_temp = current_temp;
            temperature = current_temp;
            break;

        default:
            break;
    }

    /* Set fan mode as per the setting */
    update_fan_mode(current_settings.thermostat_setting.fan_mode);

    /* Set audio level as per the setting */
    update_thermostat_volume(current_settings.audio.level);

    /* Set display brightness as per the setting */
    update_display_brightness(current_settings.display_setting.brightness);

    /* Set screen idle timeout as per the setting */
    set_idle_timeout(current_settings.system.idle_timeout);

    if(!current_settings.isTutorial){
        lv_obj_add_flag(ui_descriptionPanel, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ui_arrow, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ui_skipTutorial, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ui_descriptionPanelSecondary, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ui_descriptionLabelSecondary, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(ui_startTutorial, "  Start Tutorial  ");
    }
    else{
        lv_label_set_text(ui_startTutorial, "  End Tutorial  ");
    }
}

/*******************************************************************************
* Function Name: change_idle_timeout
********************************************************************************
* Summary:
*  Handles the idle timeout change event from the UI.
*
* Parameters:
*  e - Pointer to the LVGL event.
*
* Return:
*  void
*
*******************************************************************************/
void change_idle_timeout(lv_event_t *e)
{
    CY_UNUSED_PARAMETER(e);

    /* Read timeout value from the UI drop down element */
    uint16_t timeout = lv_dropdown_get_selected(ui_timeoutdropdown);
    LOG_INFO(CYLF_DEF, "Idle Timeout: %d\n", timeout);

    /* Set screen timeout period */
    set_idle_timeout(timeout);

    /* Stop currently running timer */
    stop_active_state_timer();

    /* Start inactivity timer with updated timeout period */
    start_inactivity_timer();
}

/*******************************************************************************
* Function Name: update_temperature
********************************************************************************
* Summary:
*  Handles the event to update the target temperature from the UI.
*
* Parameters:
*  e - Pointer to the LVGL event.
*
* Return:
*  void
*
*******************************************************************************/
void update_temperature(lv_event_t *e)
{
    CY_UNUSED_PARAMETER(e);

    /* Read set temperature value from temperature arc */
    int temperature = lv_arc_get_value(ui_ArcTempControl);
    if (dev_unit == TEMP_UNIT_CELSIUS)
    {
        LOG_INFO(CYLF_DEF, "Temperature arc value: %d\n", temperature);
    }
    else
    {
        LOG_INFO(CYLF_DEF, "Temperature arc value: %d\n", temperature);
    }

    /* Set the target temperature to the temperature set by
     *  the arc UI element */
    update_device_temp((uint8_t) temperature);
}

/*******************************************************************************
* Function Name: update_final_temperature
********************************************************************************
* Summary:
*  Updates the final temperature over IPC if device is connected.
*
* Parameters:
*  e - Pointer to the LVGL event.
*
* Return:
*  void
*
*******************************************************************************/
void update_final_temperature(lv_event_t *e)
{
    /* Update status over MApp/Cloud if connected. */
    if(true == is_device_connected)
    {
        /* Send target temp., current temp. and remaining time data
         *  to M33 core using IPC */
        update_temperature_data_ipc(&dev_info);
    }
}

/*******************************************************************************
* Function Name: update_thermostat_mode_timer
********************************************************************************
* Summary:
*  Updates the temperature adjustment timer based on thermostat mode.
*
* Parameters:
*  void
*
* Return:
*  void
*
*******************************************************************************/
void update_thermostat_mode_timer(void)
{
    /* Update the timer running instance based on the target
     * temperature, in case selected mode is not Off.  */
    if (dev_current_mode != MODE_OFF)
    {
        /* Stop the timer if no */
        if (current_temp != target_temp)
        {
            /* Delete the current running timer */
            if (temp_timer)
            {
                lv_timer_del(temp_timer);
                temp_timer = NULL;
            }

            /* In cooling state, start the timer to decrease
             * the current temperature */
            if (current_temp > target_temp)
            {
                temp_timer = lv_timer_create(decrease_temp_step, deg2sec, NULL);
            }
            /* In heating state, start the timer to increase
             * the current temperature */
            else
            {
                temp_timer = lv_timer_create(increase_temp_step, deg2sec, NULL);
            }
        }
    }
}

/*******************************************************************************
* Function Name: set_idle_timeout
********************************************************************************
* Summary:
*  Sets the system idle timeout duration.
*
* Parameters:
*  time - The selected idle timeout.
*
* Return:
*  void
*
*******************************************************************************/
void set_idle_timeout(idle_timeout_t time)
{
    switch (time)
    {
        case TIMEOUT_3S:
            LOG_INFO(CYLF_DEF, "Timeout = 3 seconds\n");
            break;

        case TIMEOUT_5S:
            LOG_INFO(CYLF_DEF, "Timeout = 5 seconds\n");
            break;

        case TIMEOUT_10S:
            LOG_INFO(CYLF_DEF, "Timeout = 10 seconds\n");
            break;

        case TIMEOUT_20S:
            LOG_INFO(CYLF_DEF, "Timeout = 20 seconds\n");
            break;

        case TIMEOUT_30S:
            LOG_INFO(CYLF_DEF, "Timeout = 30 seconds\n");
            break;

        case TIMEOUT_NEVER:
            LOG_INFO(CYLF_DEF, "Timeout = Never\n");
            break;

        default:
            LOG_ERROR(CYLF_DEF, "Invalid timeout option\n");
            break;
    }

    /* Set device settings timeout as received */
    current_settings.system.idle_timeout = time;

    /* Update the UI element as per recieved timeout */
    lv_dropdown_set_selected(ui_timeoutdropdown, time);

    /* Store the updated setting in NVM */
    update_current_device_setting();
}

/*******************************************************************************
* Function Name: change_volume
********************************************************************************
* Summary:
*  Handles the volume level change from the UI.
*
* Parameters:
*  e - Pointer to the LVGL event.
*
* Return:
*  void
*
*******************************************************************************/
void change_volume(lv_event_t *e)
{
    CY_UNUSED_PARAMETER(e);

    /* Read audio level info from the drop-down UI element */
    audio_level_t level = lv_dropdown_get_selected(ui_VolumeDrodown);
    LOG_INFO(CYLF_DEF, "Selected Volume:%d\n", level);

    /* Update system volume according to the selected level */
    set_volume(level);

    /* Is device connected to MApp or Cloud */
    if (true == is_device_connected)
    {
        /* Send audio level update info to CM33 core using IPC */
        update_audio_level_ipc(level);
    }

    /* Update audio level info. */
    dev_info.preferences.audio_level = level;
}

/*******************************************************************************
* Function Name: update_thermostat_volume
********************************************************************************
* Summary:
*  Updates the thermostat volume level.
*
* Parameters:
*  level - The new volume level to be set.
*
* Return:
*  void
*
*******************************************************************************/
void update_thermostat_volume(audio_level_t level)
{
    /* Update system volume according to the passed level */
    set_volume(level);

    /* Update the drop-down UI element to this level */
    lv_dropdown_set_selected(ui_VolumeDrodown, (uint16_t) level);

    /* Update audio level info. */
    dev_info.preferences.audio_level = level;
}

/*******************************************************************************
* Function Name: set_system_unit
********************************************************************************
* Summary:
*  Handles system temperature unit change triggered by UI toggle.
*
* Parameters:
*  e - Pointer to the LVGL event.
*
* Return:
*  void
*
*******************************************************************************/
void set_system_unit(lv_event_t *e)
{
    CY_UNUSED_PARAMETER(e);

    /* Read temperature unit switch (enabled or not) */
    bool is_checked = lv_obj_has_state(ui_tempunitswitch, LV_STATE_CHECKED);

    /* If enabled, degree set to deg F */
    if (is_checked)
    {
        LOG_INFO(CYLF_DEF, "System Unit set to deg F.\n");
        current_settings.system.temperature_unit = TEMP_UNIT_FAHRENHEIT;

        /* If previous unit was deg C, convert to deg F */
        if (dev_unit == TEMP_UNIT_CELSIUS)
        {
            current_temp = CELSIUS_TO_FAHRENHEIT(current_temp);
            target_temp = CELSIUS_TO_FAHRENHEIT(target_temp);
            temperature = CELSIUS_TO_FAHRENHEIT(temperature);
        }
        /* Update UI for deg F temperature data */
        dev_unit = TEMP_UNIT_FAHRENHEIT;
        lv_label_set_text_fmt(ui_TemperatureCurrValueLbl, "%d", current_temp);
        lv_label_set_text_fmt(ui_MainTemptextLP, "%d", current_temp);
        lv_label_set_text_fmt(ui_OutdoorTempLabel, "%d°F", CELSIUS_TO_FAHRENHEIT(atoi(weather_sync_value)));
        lv_arc_set_range(ui_ArcTempControl, TEMPERATURE_DEG_F_MIN_VALUE, TEMPERATURE_DEG_F_MAX_VALUE);
        lv_arc_set_value(ui_ArcTempControl, target_temp);
        current_max_temp = TEMPERATURE_DEG_F_MAX_VALUE;
        current_min_temp = TEMPERATURE_DEG_F_MIN_VALUE;
        lv_label_set_text_fmt(ui_currenttemp, "%d°F", target_temp);

        dev_info.environment.target_temp = target_temp;
        dev_info.environment.current_temp = current_temp;
    }
    else
    {
        LOG_INFO(CYLF_DEF, "System Unit set to deg C.\n");
        current_settings.system.temperature_unit = TEMP_UNIT_CELSIUS;

        /* If previous unit was deg F, convert to deg C */
        if (dev_unit == TEMP_UNIT_FAHRENHEIT)
        {
            current_temp = FAHRENHEIT_TO_CELSIUS(current_temp);
            target_temp = FAHRENHEIT_TO_CELSIUS(target_temp);
            temperature = FAHRENHEIT_TO_CELSIUS(temperature);
        }
        /* Update UI for deg C temperature data */
        dev_unit = TEMP_UNIT_CELSIUS;
        lv_label_set_text_fmt(ui_TemperatureCurrValueLbl, "%d", current_temp);
        lv_label_set_text_fmt(ui_MainTemptextLP, "%d", current_temp);
        lv_label_set_text_fmt(ui_OutdoorTempLabel, "%d°C", atoi(weather_sync_value));
        lv_arc_set_range(ui_ArcTempControl, TEMPERATURE_DEG_C_MIN_VALUE, TEMPERATURE_DEG_C_MAX_VALUE);
        lv_arc_set_value(ui_ArcTempControl, target_temp);
        current_max_temp = TEMPERATURE_DEG_C_MAX_VALUE;
        current_min_temp = TEMPERATURE_DEG_C_MIN_VALUE;
        lv_label_set_text_fmt(ui_currenttemp, "%d°C", target_temp);
        dev_info.environment.target_temp = target_temp;
        dev_info.environment.current_temp = current_temp;
    }

    /* Send updated device config. to CM33 core using IPC */
    update_device_config_ipc();

    /* Store updated device settings */
    update_current_device_setting();
}


/*******************************************************************************
* Function Name: update_system_unit
********************************************************************************
* Summary:
*  Updates the system temperature unit and refreshes UI elements.
*
* Parameters:
*  unit - The system unit to be applied.
*
* Return:
*  void
*
*******************************************************************************/
void update_system_unit(temp_unit_t unit)
{
    /* Based on the selected system unit convert all temperature
     * reading on UI to Celcius or Faranhiet */
    if (TEMP_UNIT_FAHRENHEIT == unit)
    {
        /* If previous unit was C, convert data to F */
        if (TEMP_UNIT_CELSIUS == dev_unit)
        {
            current_temp = CELSIUS_TO_FAHRENHEIT(current_temp);
            target_temp = CELSIUS_TO_FAHRENHEIT(target_temp);
            temperature = CELSIUS_TO_FAHRENHEIT(temperature);
        }

        /* Update all labels on UI (Active and Sleep) screen */
        lv_obj_add_state(ui_tempunitswitch, LV_STATE_CHECKED);
        lv_label_set_text_fmt(ui_TemperatureCurrValueLbl, "%d", current_temp);
        lv_label_set_text_fmt(ui_MainTemptextLP, "%d", current_temp);
        lv_label_set_text_fmt(ui_OutdoorTempLabel, "%d°F", CELSIUS_TO_FAHRENHEIT(atoi(weather_sync_value)));
        lv_arc_set_range(ui_ArcTempControl, TEMPERATURE_DEG_F_MIN_VALUE, TEMPERATURE_DEG_F_MAX_VALUE);
        lv_arc_set_value(ui_ArcTempControl, target_temp);
        current_max_temp = TEMPERATURE_DEG_F_MAX_VALUE;
        current_min_temp = TEMPERATURE_DEG_F_MIN_VALUE;
        lv_label_set_text_fmt(ui_currenttemp, "%d°F", target_temp);
        dev_info.environment.target_temp = target_temp;
        dev_info.environment.current_temp = current_temp;
    }
    else if (TEMP_UNIT_CELSIUS == unit)
    {
        /* If previous unit was F, convert data to C */
        if (TEMP_UNIT_FAHRENHEIT == dev_unit)
        {
            current_temp = FAHRENHEIT_TO_CELSIUS(current_temp);
            target_temp = FAHRENHEIT_TO_CELSIUS(target_temp);
            temperature = FAHRENHEIT_TO_CELSIUS(temperature);
        }

        /* Update all labels on UI (Active and Sleep) screen */
        lv_obj_clear_state(ui_tempunitswitch, LV_STATE_CHECKED);
        lv_label_set_text_fmt(ui_TemperatureCurrValueLbl, "%d", current_temp);
        lv_label_set_text_fmt(ui_MainTemptextLP, "%d", current_temp);
        lv_label_set_text_fmt(ui_OutdoorTempLabel, "%d°C", atoi(weather_sync_value));
        lv_arc_set_range(ui_ArcTempControl, TEMPERATURE_DEG_C_MIN_VALUE, TEMPERATURE_DEG_C_MAX_VALUE);
        lv_arc_set_value(ui_ArcTempControl, target_temp);
        current_max_temp = TEMPERATURE_DEG_C_MAX_VALUE;
        current_min_temp = TEMPERATURE_DEG_C_MIN_VALUE;
        lv_label_set_text_fmt(ui_currenttemp, "%d°C", target_temp);
        dev_info.environment.target_temp = target_temp;
        dev_info.environment.current_temp = current_temp;
    }

    /* Update current temperature unit */
    current_settings.system.temperature_unit = unit;
    dev_unit = unit;

    /* Store device settings in NVM */
    update_current_device_setting();
}

/*******************************************************************************
* Function Name: get_default_device_setting
********************************************************************************
* Summary:
*  Retrieves the default device settings.
*
* Parameters:
*  settings - Pointer to store the default settings.
*
* Return:
*  void
*
*******************************************************************************/
void get_default_device_setting(device_settings_t *settings)
{
    memset(settings, 0, sizeof(device_settings_t));

    /* Copy default device settings into passed argument */
    memcpy(settings, &default_config, sizeof(device_settings_t));
}

/*******************************************************************************
* Function Name: get_current_device_setting
********************************************************************************
* Summary:
*  Retrieves the current device settings.
*
* Parameters:
*  settings - Pointer to store the current settings.
*
* Return:
*  void
*
*******************************************************************************/
void get_current_device_setting(device_settings_t *settings)
{
    memset(settings, 0, sizeof(device_settings_t));

    /* Copy current device settings into passed argument */
    memcpy(settings, &current_settings, sizeof(device_settings_t));
}

/*******************************************************************************
* Function Name: set_current_device_setting
********************************************************************************
* Summary:
*  Updates the current device settings.
*
* Parameters:
*  settings - Pointer containing new settings.
*
* Return:
*  void
*
*******************************************************************************/
void set_current_device_setting(device_settings_t *settings)
{
    /* Update current device settings with the ones given as argument */
    current_settings.audio.level = settings->audio.level;
    current_settings.display_setting.brightness = settings->display_setting.brightness;
    current_settings.thermostat_setting.fan_mode = settings->thermostat_setting.fan_mode;
    current_settings.thermostat_setting.mode = settings->thermostat_setting.mode;
    current_settings.is_available = settings->is_available;
    current_settings.system.idle_timeout = settings->system.idle_timeout;
    current_settings.system.temperature_unit = settings->system.temperature_unit;
    current_settings.isTutorial = settings->isTutorial;
}

/*******************************************************************************
* Function Name: update_current_device_setting
********************************************************************************
* Summary:
*  Flags the device to write current settings to EEPROM.
*
* Parameters:
*  void
*
* Return:
*  void
*
*******************************************************************************/
void update_current_device_setting(void)
{
    eeprom_wr_setting = true;
}

/*******************************************************************************
* Function Name: device_factory_reset
********************************************************************************
* Summary:
*  Performs a factory reset of the device.
*
* Parameters:
*  e - Pointer to the LVGL event.
*
* Return:
*  void
*
*******************************************************************************/
void device_factory_reset(lv_event_t *e)
{
    CY_UNUSED_PARAMETER(e);
    device_settings_t read_settings = { 0 };

    /* Read device default settings */
    get_default_device_setting(&read_settings);

    /* Write default settings to current settings */
    set_current_device_setting(&read_settings);

    /* Store updated current settings to NVM*/
    update_current_device_setting();

    /* Update flags as per the settings*/
    dev_current_mode = read_settings.thermostat_setting.mode;
    dev_fan_mode = read_settings.thermostat_setting.fan_mode;
    brightness_level = read_settings.display_setting.brightness;
    audio_level = read_settings.audio.level;
    temperature = DEFAULT_STARTUP_TEMPERATURE;
    current_temp = DEFAULT_STARTUP_TEMPERATURE;
    target_temp = DEFAULT_STARTUP_TEMPERATURE;

    load_thermostat_config(current_settings.thermostat_setting.mode);
    update_device_config_ipc();

    _ui_flag_modify(ui_ArcGroup, LV_OBJ_FLAG_HIDDEN, _UI_MODIFY_FLAG_REMOVE);
    lv_disp_load_scr(ui____initial_actions0);
    lv_disp_load_scr(ui_BootScreen);

    /* Send factory reset command over IPC
     * to delete the WiFi credentials */
    request_wifi_delete_ipc();

    update_device_connection_state(DEV_ST_UNPROVISIONED);

    /* Update device provisioned state */
    is_device_provisioned = false;

    /*Reset tutorial Panels*/
    if(current_settings.isTutorial) {   
        lv_obj_clear_flag(ui_skipTutorial, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(ui_descriptionPanel, LV_OBJ_FLAG_HIDDEN);                                                      
        setDescArc();
    }
    else {
        toggleTutorial(); 
    }

    handle_http_factory_reset();
}

/*******************************************************************************
* Function Name: update_device_config_ipc
********************************************************************************
* Summary:
*  Sends the current device configuration to the CM33 core.
*
* Parameters:
*  void
*
* Return:
*  void
*
*******************************************************************************/
void update_device_config_ipc(void)
{
    /* Get snaphsot of current configuration */
    dev_info.environment.current_temp = current_temp;
    dev_info.environment.target_temp = target_temp;

    /*Zero or unknown humidity values should be handled by the mobile app*/
    dev_info.environment.current_humidity = get_current_humidity_pct();

    dev_info.thermostat_settings.fan_speed = dev_fan_mode;
    dev_info.thermostat_settings.mode = dev_current_mode;
    dev_info.thermostat_settings.time_remains = 0;
    dev_info.preferences.display_brightness = brightness_level;
    dev_info.preferences.audio_level = audio_level;
    dev_info.thermostat_settings.temp_unit = dev_unit;

    /* Send configuration to CM33 core using IPC */
    send_device_config(&dev_info);
}

/*******************************************************************************
* Function Name: start_inactivity_timer
********************************************************************************
* Summary:
*  Starts the inactivity timer based on the current idle timeout setting.
*
* Parameters:
*  void
*
* Return:
*  void
*
*******************************************************************************/
void start_inactivity_timer(void)
{
    uint32_t addn_timeout_ms = 0;
    /* Start inactivity timer if Screen timeout is not set to Never */
    if (TIMEOUT_NEVER != current_settings.system.idle_timeout)
    {
        if (person_detected == true)
        {
            /* Add additional timeout when person detected so screen doesn't immediately transition */
            addn_timeout_ms = 5000 /* 5 seconds */;
        }
        /* Start timer with screen timeout duration */
        start_active_state_timer(addn_timeout_ms + get_timeout_ms(current_settings.system.idle_timeout));
    }
}

/*******************************************************************************
* Function Name: start_inactivity_timer_addn
********************************************************************************
* Summary:
*  Starts the inactivity timer with an additional timeout in milliseconds.
*
* Parameters:
*  additional_timeout_ms - Milliseconds to add to the configured timeout.
*
* Return:
*  void
*
*******************************************************************************/
void start_inactivity_timer_addn(uint32_t additional_timeout_ms)
{
    /* If configured timeout is set to never, do nothing */
    if (TIMEOUT_NEVER == current_settings.system.idle_timeout)
    {
        return;
    }

    uint32_t base_ms = get_timeout_ms(current_settings.system.idle_timeout);

    start_active_state_timer(base_ms + additional_timeout_ms);
}

/*******************************************************************************
* Function Name: cancel_dev_conn_current_opt
********************************************************************************
* Summary:
*  Handles cancel device connection event and updates the device state.
*
* Parameters:
*  e - Pointer to the LVGL event.
*
* Return:
*  void
*
*******************************************************************************/
void cancel_dev_conn_current_opt(lv_event_t *e)
{
    CY_UNUSED_PARAMETER(e);

    /* Update UI if cancel button event is received */
    if (DEV_ST_BLE_ADVERTISING == dev_current_conn_state)
    {
        /* Update cancel event over IPC */
        update_connection_state_ipc(DEV_ST_BLE_ADVERTISEMENT_CANCEL);

        /* If last state was un-provisioned, move back to un-provisioned state */
        if ((!is_device_provisioned) || (DEV_ST_UNPROVISIONED == dev_last_conn_state))
        {
            lv_obj_clear_flag(ui_commissionMapp, LV_OBJ_FLAG_HIDDEN);
            update_device_connection_state(DEV_ST_UNPROVISIONED);
        }
        else if ((is_device_provisioned) && (DEV_ST_BLE_ADVERTISING == dev_last_conn_state))
        {
            /* Clear auto-reconnect switch */
            lv_obj_clear_state(ui_Switch2, LV_STATE_CHECKED);

            /* Update UI with Wi-Fi connecting status */
            update_device_connection_state(DEV_ST_WIFI_CONNECTING);

            /* Send switch to Wi-Fi command to CM33 core using IPC */
        }
        else
        {
            lv_obj_clear_flag(ui_commissionMapp, LV_OBJ_FLAG_HIDDEN);
            update_device_connection_state(DEV_ST_UNPROVISIONED);
        }
    }
    else if (DEV_ST_WIFI_CONNECTING == dev_current_conn_state)
    {
        /* Update cancel event over IPC */
        update_connection_state_ipc(DEV_ST_WIFI_CONNECTION_CANCEL);

        /* If last state was un-provisioned, move back to un-provisioned state */
        if (DEV_ST_UNPROVISIONED == dev_last_conn_state)
        {
            lv_obj_clear_flag(ui_commissionMapp, LV_OBJ_FLAG_HIDDEN);
            update_device_connection_state(DEV_ST_UNPROVISIONED);
        }
        else if (DEV_ST_WIFI_CONNECTING == dev_last_conn_state)
        {
            /* Clear auto-reconnect switch */
            lv_obj_clear_state(ui_Switch2, LV_STATE_CHECKED);

            /* Update UI with Wi-Fi disconnected status */
            if (true == ble_conn_state)
            {
                update_device_connection_state(DEV_ST_BLE_CONNECTED);
            }
            else
            {
                /* Check if connected to WiFi. */
                if (true == wifi_conn_state)
                {
                    update_device_connection_state(DEV_ST_WIFI_DISCONNECTED);
                }
                else
                {
                    update_device_connection_state(DEV_ST_BLE_ADVERTISING);
                }
            }
        }
        else
        {
            lv_obj_clear_flag(ui_commissionMapp, LV_OBJ_FLAG_HIDDEN);
            update_device_connection_state(DEV_ST_UNPROVISIONED);
        }
    }
    else if (DEV_ST_CLOUD_CONNECTING == dev_current_conn_state)
    {
        /* Update cancel event over IPC */
        update_connection_state_ipc(DEV_ST_CLOUD_CONNECTION_CANCEL);

        /* If last state was un-provisioned, move back to un-provisioned state */
        if (DEV_ST_UNPROVISIONED == dev_last_conn_state)
        {
            lv_obj_clear_flag(ui_commissionMapp, LV_OBJ_FLAG_HIDDEN);
            update_device_connection_state(DEV_ST_UNPROVISIONED);
        }
        else if (DEV_ST_CLOUD_CONNECTING == dev_last_conn_state)
        {
            /* Clear auto-reconnect switch */
            lv_obj_clear_state(ui_Switch2, LV_STATE_CHECKED);

            /* Update UI with Cloud disconnected status */
            if (true == ble_conn_state)
            {
                update_device_connection_state(DEV_ST_BLE_CONNECTED);
            }
            else
            {
                /* If Wi-Fi connected */
                if(true == wifi_conn_state)
                {
                    update_device_connection_state(DEV_ST_CLOUD_DISCONNECTED);
                }
                else
                {
                    update_device_connection_state(DEV_ST_BLE_ADVERTISING);
                }
            }
        }
        else
        {
            lv_obj_clear_flag(ui_commissionMapp, LV_OBJ_FLAG_HIDDEN);
            update_device_connection_state(DEV_ST_UNPROVISIONED);
        }
    }
    else
    {
        LOG_ERROR(CYLF_DEF, "Un-handled cancel event received: Current event: %d\n", dev_current_conn_state);
    }
}

/*******************************************************************************
* Function Name: wifi_status_handler
********************************************************************************
* Summary:
*  Handles Wi-Fi status and enqueues a notification.
*
* Parameters:
*  void
*
* Return:
*  void
*
*******************************************************************************/
void wifi_status_handler(){

    if (wifi_conn_state){
        enqueue_notification(NOTIFY_NETWORK_STATUS, NOTIF_SUCCESS, DEV_ST_WIFI_CONNECTED);
    }
    else{
        enqueue_notification(NOTIFY_NETWORK_STATUS, NOTIF_FAIL, DEV_ST_WIFI_DISCONNECTED);
    }
    
}

/*******************************************************************************
* Function Name: auto_reconnect_cb
********************************************************************************
* Summary:
*  Handles the auto-reconnect switch event to set connection retries.
*
* Parameters:
*  e - Pointer to the LVGL event.
*
* Return:
*  void
*
*******************************************************************************/
void auto_reconnect_cb(lv_event_t *e)
{
    CY_UNUSED_PARAMETER(e);

    /* Update connection attempt based on the value of auto-reconnect switch */
    if (lv_obj_has_state(ui_Switch2, LV_STATE_CHECKED))
    {
        /* Update infinite connection attempts based on current conn state */
        if ((DEV_ST_WIFI_CONNECTING == dev_current_conn_state) ||
                (DEV_ST_WIFI_DISCONNECTED == dev_current_conn_state))
        {
            /* Update UI with Wi-Fi connection screen */
            update_device_connection_state(DEV_ST_WIFI_CONNECTING);

            /* Update connection retries over IPC */
            update_connection_retries_ipc(WIFI_CONN_RETRY_INFINITE);
            wifi_reconnect_retry = WIFI_CONN_RETRY_INFINITE;
        }
        else if ((DEV_ST_CLOUD_CONNECTING == dev_current_conn_state)
                || (DEV_ST_CLOUD_DISCONNECTED == dev_current_conn_state))
        {
            /* Update UI with Cloud connection screen */
            update_device_connection_state(DEV_ST_CLOUD_CONNECTING);

            /* Update connection retries over IPC */
            update_connection_retries_ipc(CLOUD_CONN_RETRY_INFINITE);
            cloud_reconnect_retry = CLOUD_CONN_RETRY_INFINITE;
        }
        else if (DEV_ST_BLE_ADVERTISING == dev_current_conn_state)
        {
            /* Update UI with BLE advertising screen */
            update_device_connection_state(DEV_ST_BLE_ADVERTISING);

            /* Update connection retries over IPC */
            update_connection_retries_ipc(BLE_CONN_RETRY_INFINITE);
            ble_reconnect_retry = BLE_CONN_RETRY_INFINITE;
        }
    }
    else
    {
        /* Update limited connection attempts based on current conn. state */
        if ((DEV_ST_WIFI_CONNECTING == dev_current_conn_state) ||
                (DEV_ST_WIFI_DISCONNECTED == dev_current_conn_state))
        {
            update_connection_retries_ipc(WIFI_CONN_RETRY_LIMITED);
            wifi_reconnect_retry = WIFI_CONN_RETRY_LIMITED;
        }
        else if ((DEV_ST_CLOUD_CONNECTING == dev_current_conn_state)
                || (DEV_ST_CLOUD_DISCONNECTED == dev_current_conn_state))
        {
            update_connection_retries_ipc(CLOUD_CONN_RETRY_LIMITED);
            cloud_reconnect_retry = CLOUD_CONN_RETRY_LIMITED;
        }
        else if (DEV_ST_BLE_ADVERTISING == dev_current_conn_state)
        {
            update_connection_retries_ipc(BLE_CONN_RETRY_LIMITED);
            ble_reconnect_retry = BLE_CONN_RETRY_LIMITED;
        }
    }
}

/*******************************************************************************
* Function Name: switch_to_wifi
********************************************************************************
* Summary:
*  Switches the device connection to Wi-Fi.
*
* Parameters:
*  e - Pointer to the LVGL event.
*
* Return:
*  void
*
*******************************************************************************/
void switch_to_wifi(lv_event_t *e)
{
    CY_UNUSED_PARAMETER(e);

    /* Update UI with Wi-Fi connecting status */
    update_device_connection_state(DEV_ST_WIFI_CONNECTING);

    /* Send switch to Wi-Fi command to CM33 core using IPC */
    update_switch_wifi_ipc();
}


/*******************************************************************************
* Function Name: check_humidity_overshoot
********************************************************************************
* Summary:
*  Checks if humidity exceeds the threshold.
*
* Parameters:
*  rh - Relative humidity value.
*
* Return:
*  bool - true if humidity exceeds threshold.
*
*******************************************************************************/
bool check_humidity_overshoot(uint16_t rh)
{ 
    return (rh > RH_THRESHOLD)?true:false;
}

/*******************************************************************************
* Function Name: update_humidity_data_ui
********************************************************************************
* Summary:
*  Updates the humidity data on the UI.
*
* Parameters:
*  rh - Relative humidity value.
*
* Return:
*  void
*
*******************************************************************************/
void update_humidity_data_ui(uint16_t rh)
{
    if(rh > LIMIT_RH && rh < MAX_RH)
    {
        lv_label_set_text_fmt(ui_activehumidity, "%u %%", rh);
        if(rh < 40 || rh > 55)
        {
            lv_obj_set_style_text_color(ui_activehumidity, lv_color_hex(0xFF9737), LV_PART_MAIN | LV_STATE_DEFAULT);
        }
        else
        {
            lv_obj_set_style_text_color(ui_activehumidity, lv_color_hex(0xFAF8F8), LV_PART_MAIN | LV_STATE_DEFAULT);
        }
        send_humidity_to_m33(rh);
    }
}

/*******************************************************************************
* Function Name: switch_to_active_screen
********************************************************************************
* Summary:
*  Switches from low-power screen to active screen.
*
* Parameters:
*  void
*
* Return:
*  void
*
*******************************************************************************/
void switch_to_active_screen(void)
{
    lv_obj_t *current_screen = lv_scr_act();

    if(current_screen == ui_LPScreen)
    {
        _ui_screen_change(&ui_ActiveScreen, LV_SCR_LOAD_ANIM_FADE_ON, 150, 0, &ui_ActiveScreen_screen_init);
        app_state = APP_ST_ACTIVE;
        
        /* Update sensor sampling interval to 1s */
        /* Set sensor sampling interval to IDLE state */
        set_sensor_sampling_interval(SENSOR_SAMPLING_INTERVAL_ACTIVE);
    }

    if (popup_overlay_visible && !popup_auto_hide_pending)
    {
        lv_obj_add_flag(ui_popupoverlay, LV_OBJ_FLAG_HIDDEN);
        _ui_flag_modify(ui_ArcGroup, LV_OBJ_FLAG_HIDDEN, _UI_MODIFY_FLAG_REMOVE);
        popup_overlay_visible = false;
        wifi_popup_state = false;
        display_fan_anim();
        display_temp_change_anim();
    }

    start_inactivity_timer();
}

/*******************************************************************************
* Function Name: switch_to_ble
********************************************************************************
* Summary:
*  Switches the device connection to BLE.
*
* Parameters:
*  e - Pointer to the LVGL event.
*
* Return:
*  void
*
*******************************************************************************/
void switch_to_ble(lv_event_t *e)
{
    /* Update UI with BLE ADV status */
    update_device_connection_state(DEV_ST_BLE_ADVERTISING);

    /* Send switch to BLE command to CM33 core using IPC */
    send_switch_to_ble_cmd();

    is_device_connected = false;
}


/*******************************************************************************
* Function Name: update_date_time_rtc
********************************************************************************
* Summary:
*  Reads the date and time from the UI and updates the RTC.
*
* Parameters:
*  e - Pointer to the LVGL event.
*
* Return:
*  void
*
*******************************************************************************/
void update_date_time_rtc(lv_event_t * e)
{
    /* Ensure the event is a click event */
    if (lv_event_get_code(e) != LV_EVENT_CLICKED)
    {
        return;
    }

    /* A temporary structure to hold the new time and date */
    cy_stc_rtc_config_t new_time = {0};
    cy_en_rtc_status_t rtc_status = CY_RTC_BAD_PARAM;
    uint32_t retry_count = 5;

    /* Get current RTC config. */
    Cy_RTC_GetDateAndTime(&new_time);


    /* Calculate the day of the week from the date */
    new_time.dayOfWeek = Cy_RTC_ConvertDayOfWeek(
        new_time.date,
        new_time.month,
        (new_time.year + RTC_CENTURY));


    /* Validate the time values */
    if (new_time.sec < 60 && new_time.min < 60 && new_time.hour < 24)
    {
        /* Set the new date and time in the RTC with a retry mechanism */
        do
        {
            rtc_status = Cy_RTC_SetDateAndTime(&new_time);
            retry_count--;
            if (rtc_status != CY_RTC_SUCCESS)
            {
                /* Delay for 1ms before retrying to allow the RTC to become ready */
                vTaskDelay(pdMS_TO_TICKS(1));
            }
        } while ((rtc_status != CY_RTC_SUCCESS) && (retry_count > 0));


        if (rtc_status == CY_RTC_SUCCESS)
        {
            /* Update current time stamp on UI */
            update_time_on_ui();
            update_timestamp = true;
        }
        else
        {
            /* Handle error, e.g., RTC was busy */
            APP_ERROR(1);
        }

        stop_minute_sync_timer();
        start_minute_sync_timer();

    }
}

/*******************************************************************************
* Function Name: display_fan_anim
********************************************************************************
* Summary:
*  Starts the fan animation based on the current fan mode.
*
* Parameters:
*  void
*
* Return:
*  void
*
*******************************************************************************/
void display_fan_anim(void)
{
    if (lv_screen_active() == ui_ActiveScreen)
    {
        lv_obj_add_flag(ui_voicecmdcontainer, LV_OBJ_FLAG_HIDDEN);
        /* Update the fan animation based on the global fan mode variable. */
        update_fan_mode(dev_fan_mode);
    }
}

/*******************************************************************************
* Function Name: stop_fan_anim
********************************************************************************
* Summary:
*  Stops all fan animations and hides them.
*
* Parameters:
*  void
*
* Return:
*  void
*
*******************************************************************************/
void stop_fan_anim(void)
{
    /* Delete fan mode animation for all possible states. */
    lv_anim_del(ui_fanoff, NULL);
    lv_anim_del(ui_fanactivehigh, NULL);
    lv_anim_del(ui_fanactivelow, NULL);
    lv_anim_del(ui_fanactivemed, NULL);

    /* Hide all the animated fan images initially. */
    lv_obj_add_flag(ui_fanoff, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ui_fanactivehigh, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ui_fanactivelow, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ui_fanactivemed, LV_OBJ_FLAG_HIDDEN);

    /* Clear the hidden flag for the main fan image and start the animation from a stopped state. */
     lv_obj_clear_flag(ui_fanoff, LV_OBJ_FLAG_HIDDEN);
     fanspeed_Animation(ui_fanoff, 0);
}

/*******************************************************************************
* Function Name: update_homescreen_connectivity_state
********************************************************************************
* Summary:
*  Updates the UI to reflect the current connectivity state.
*
* Parameters:
*  void
*
* Return:
*  void
*
*******************************************************************************/
void update_homescreen_connectivity_state(void)
{
    update_device_connection_state(dev_current_conn_state);
}

/*******************************************************************************
* Function Name: stop_homescreen_connectivity_state
********************************************************************************
* Summary:
*  Stops all connectivity animations and resets the UI state.
*
* Parameters:
*  void
*
* Return:
*  void
*
*******************************************************************************/
void stop_homescreen_connectivity_state(void)
{
    const char *state_text = "Unknown";

    /* Hide all hidden widgets (images, button, labels) initially */
    lv_obj_add_flag(ui_commissionMapp, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ui_commissionKeyboard, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ui_autoreconnectpanel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ui_connstatekeyboardbtn, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ui_devconnstatecontianer, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ui_qrcodecontainer, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(ui_devconnstatecontianer, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ui_cloudconnected, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ui_wificonnecting120, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ui_clouddisconn120, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ui_bleadv, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ui_devstateimg, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ui_wificonnectedimg, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ui_wifidisabledimg, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ui_bleconnected120, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ui_wifi, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ui_homebleconnected, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ui_bleconnected50, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ui_homewifidisconnimg, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ui_homewifidisconnimg, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ui_homecloudconnimg, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ui_qrcodebtn, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ui_wifideletebtn, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ui_qrcodebtn, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ui_cloudconnecting, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ui_connectionprg1, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ui_connectionprg2, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ui_connectionprg3, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ui_switchtowifilbl, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ui_homebleconnected, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ui_bleconnected50, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ui_bleswitchbtn, LV_OBJ_FLAG_HIDDEN);

    /* Delete all running animations associated with connectivity. */
    lv_anim_del(ui_bleadv, NULL);
    lv_anim_del(ui_wificonnecting120, NULL);
    lv_anim_del(ui_wifi, NULL);
    lv_anim_del(ui_homebleconnected, NULL);
    lv_anim_del(ui_connectionprg1, NULL);
    lv_anim_del(ui_connectionprg2, NULL);
    lv_anim_del(ui_connectionprg3, NULL);

    /* Update the UI based on the current device connection state. */
    switch (dev_current_conn_state)
    {
        case DEV_ST_CLOUD_CONNECTING:
            /* Update icon on home screen */
            lv_obj_clear_flag(ui_homewifidisconnimg, LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_style_opa(ui_homewifidisconnimg, 255, 0);
            break;

        case DEV_ST_UNPROVISIONED:
        case DEV_ST_WIFI_CONNECTING:
            /* Update icon on home screen */
            lv_obj_add_flag(ui_wifi, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(ui_homebleconnected, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(ui_wifistatic, LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_style_opa(ui_wifistatic, 130, LV_PART_MAIN | LV_STATE_DEFAULT);
            break;

        case DEV_ST_BLE_ADVERTISING:
        case DEV_ST_BLE_PAIRING:
            /* Update icon on home screen */
            lv_obj_clear_flag(ui_homebleconnected, LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_style_opa(ui_homebleconnected, 255, 0);
            break;

        case DEV_ST_WIFI_DISCONNECTED:
            /* Update icon on home screen */
            lv_obj_clear_flag(ui_homewifidisconnimg, LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_style_opa(ui_homewifidisconnimg, 255, 0);
            break;

        case DEV_ST_BLE_CONNECTED:
            /* Update icon on home screen */
            lv_obj_clear_flag(ui_homebleconnected, LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_style_opa(ui_homebleconnected, 255, 0);
            break;

        default:
            break;
    }

    /* Update device state label in popup screen */
    lv_label_set_text(ui_devstatelabel, state_text);
}

/*******************************************************************************
* Function Name: display_temp_change_anim
********************************************************************************
* Summary:
*  Displays the temperature change animation.
*
* Parameters:
*  void
*
* Return:
*  void
*
*******************************************************************************/
void display_temp_change_anim(void)
{
    if (current_state == STATE_HEATING)
    {
        lv_obj_remove_flag(ui_currenttemp, LV_OBJ_FLAG_HIDDEN);
    }
    else if (current_state == STATE_COOLING)
    {
        lv_obj_remove_flag(ui_currenttemp, LV_OBJ_FLAG_HIDDEN);
    }
}

/*******************************************************************************
* Function Name: app_timer_handler
********************************************************************************
* Summary:
*  Timer handler to transition to low-power screen on inactivity.
*
* Parameters:
*  timer - Pointer to the LVGL timer.
*
* Return:
*  void
*
*******************************************************************************/
void app_timer_handler(lv_timer_t *timer)
{
    _ui_screen_change(&ui_LPScreen, LV_SCR_LOAD_ANIM_FADE_ON, 230, 0, &ui_LPScreen_screen_init);
    lv_timer_del(app_timer);
    app_timer = NULL;
    app_state = APP_ST_IDLE;

    /* Set sensor sampling interval to IDLE state */
    set_sensor_sampling_interval(SENSOR_SAMPLING_INTERVAL_IDLE);
}

/*******************************************************************************
* Function Name: start_active_state_timer
********************************************************************************
* Summary:
*  Starts the active state timer with the given timeout.
*
* Parameters:
*  timeout_ms - Timeout duration in milliseconds.
*
* Return:
*  void
*
*******************************************************************************/
void start_active_state_timer(uint32_t timeout_ms)
{
    if (app_timer == NULL)
    {
        /* Create timer with LV_TIMER_RUN_ONCE flag */
        app_timer = lv_timer_create_basic(); /* Basic timer (manual config) */
        lv_timer_set_cb(app_timer, app_timer_handler);
        lv_timer_set_repeat_count(app_timer, 1); /* One-shot */
    }

    lv_timer_set_period(app_timer, timeout_ms);
    lv_timer_reset(app_timer); /* Restart timer countdown */
    lv_timer_resume(app_timer); /* Ensure it's active */
    app_state = APP_ST_ACTIVE;
}

/*******************************************************************************
* Function Name: stop_active_state_timer
********************************************************************************
* Summary:
*  Stops the active state timer.
*
* Parameters:
*  void
*
* Return:
*  void
*
*******************************************************************************/
void stop_active_state_timer(void)
{
    if (app_timer != NULL)
    {
        lv_timer_pause(app_timer);
        lv_timer_reset(app_timer);
    }
}

/*******************************************************************************
* Function Name: mic_icon_click_handler
********************************************************************************
* Summary:
*  Handles the microphone icon click event.
*
* Parameters:
*  e - Pointer to the LVGL event.
*
* Return:
*  void
*
*******************************************************************************/
void mic_icon_click_handler(lv_event_t * e)
{
    is_mic_clicked = true;
}

/*******************************************************************************
* Function Name: setDescPresence
********************************************************************************
* Summary:
*  Sets the presence indicator info widgets position for tutorial.
*
* Parameters:
*  void
*
* Return:
*  void
*
*******************************************************************************/
void setDescPresence(){
    current_widget = DESC_PRESENCE;          
    lv_obj_add_flag(ui_descriptionPanelSecondary, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ui_descriptionLabelSecondary, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ui_arrowSecondary, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(ui_arrow, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_x(ui_descriptionPanel, 73);
    lv_obj_set_y(ui_descriptionPanel, 195);
    lv_obj_set_x(ui_arrow, -117);
    lv_obj_set_y(ui_arrow, 243);
    lv_obj_set_style_transform_rotation(ui_arrow, -900, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_label_set_text(ui_descriptionLabel, "  Move Away to test\n  Absence Detection");
    lv_timer_reset(tutorialTimer); 
    switchTimer = false;
}

/*******************************************************************************
* Function Name: setDescCommand
********************************************************************************
* Summary:
*  Sets the voice command info widgets position for tutorial.
*
* Parameters:
*  void
*
* Return:
*  void
*
*******************************************************************************/
void setDescCommand(){
    current_widget = DESC_COMMAND;           
    lv_obj_clear_flag(ui_descriptionPanelSecondary, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(ui_descriptionLabelSecondary, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ui_arrowSecondary, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(ui_arrow, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_x(ui_descriptionPanel, -95);
    lv_obj_set_y(ui_descriptionPanel, 195);
    lv_obj_set_x(ui_arrow, 114);
    lv_obj_set_y(ui_arrow, 232);
    lv_obj_set_x(ui_arrowSecondary, 240);
    lv_obj_set_y(ui_arrowSecondary, -65);
    lv_obj_set_style_transform_rotation(ui_arrow, 900, LV_PART_MAIN | LV_STATE_DEFAULT);  
    lv_obj_set_style_transform_rotation(ui_arrowSecondary, 800, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_label_set_text(ui_descriptionLabel, "           Tap to view\nList of voice commands");
    lv_label_set_text(ui_descriptionLabelSecondary, " Say \"Ok Thermostat\" to\n activate voice command"); 
    lv_timer_reset(tutorialTimer);
    switchTimer = true;
}

/*******************************************************************************
* Function Name: setDescRightPanel
********************************************************************************
* Summary:
*  Sets the right panel info widgets position for tutorial.
*
* Parameters:
*  void
*
* Return:
*  void
*
*******************************************************************************/
void setDescRightPanel(){
    current_widget = DESC_RIGHT_PANEL;              
    lv_obj_clear_flag(ui_arrowSecondary, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ui_descriptionPanelSecondary, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ui_descriptionLabelSecondary, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ui_arrow, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_x(ui_descriptionPanel, -12);
    lv_obj_set_y(ui_descriptionPanel, 156);
    lv_obj_set_x(ui_arrowSecondary, 132);
    lv_obj_set_y(ui_arrowSecondary, 63);
    lv_obj_set_style_transform_rotation(ui_arrowSecondary, 250, LV_PART_MAIN | LV_STATE_DEFAULT); 
    lv_label_set_text(ui_descriptionLabel, "Toggle Through Modes\n        and Fan speed");
}

/*******************************************************************************
* Function Name: setDescLeftPanel
********************************************************************************
* Summary:
*  Sets the left panel info widgets position for tutorial.
*
* Parameters:
*  void
*
* Return:
*  void
*
*******************************************************************************/
void setDescLeftPanel(){
    current_widget = DESC_LEFT_PANEL;           
    lv_obj_clear_flag(ui_arrowSecondary, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ui_arrow, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ui_descriptionPanelSecondary, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ui_descriptionLabelSecondary, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_x(ui_descriptionPanel, -12);
    lv_obj_set_y(ui_descriptionPanel, 156);
    lv_obj_set_x(ui_arrowSecondary, -163);
    lv_obj_set_y(ui_arrowSecondary, 70);
    lv_obj_set_style_transform_rotation(ui_arrowSecondary, -250, LV_PART_MAIN | LV_STATE_DEFAULT);  
    lv_label_set_text(ui_descriptionLabel, "Toggle Through Settings\n       and Connectivity");
}

/*******************************************************************************
* Function Name: setDescArc
********************************************************************************
* Summary:
*  Sets the arc info widgets position for tutorial.
*
* Parameters:
*  void
*
* Return:
*  void
*
*******************************************************************************/
void setDescArc(){
    current_widget = DESC_ARC;
    lv_obj_add_flag(ui_arrowSecondary, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(ui_arrow, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ui_descriptionPanelSecondary, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ui_descriptionLabelSecondary, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_x(ui_descriptionPanel, -12); /*  */
    lv_obj_set_y(ui_descriptionPanel, 192); /*  */
    lv_obj_set_x(ui_arrow, -55);
    lv_obj_set_y(ui_arrow, 115);
    lv_obj_set_style_transform_rotation(ui_arrow, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_label_set_text(ui_descriptionLabel, "         Control the\nTemperature Set point");
}

/*******************************************************************************
* Function Name: descHideAll
********************************************************************************
* Summary:
*  Hides all tutorial info widgets.
*
* Parameters:
*  void
*
* Return:
*  void
*
*******************************************************************************/
void descHideAll(){
    current_settings.isTutorial = false;
    device_settings_t settings = { 0 };
    get_current_device_setting(&settings);
    app_eeprom_write(&settings);
    lv_obj_add_flag(ui_descriptionPanel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ui_arrowSecondary, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ui_arrow, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ui_skipTutorial, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ui_descriptionPanelSecondary, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ui_descriptionLabelSecondary, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(ui_startTutorial, "  Start Tutorial  ");
}

/*******************************************************************************
* Function Name: nextDescription
********************************************************************************
* Summary:
*  Moves to the next tutorial description.
*
* Parameters:
*  void
*
* Return:
*  void
*
*******************************************************************************/
void nextDescription(){
    switch (current_widget)
    {
        case DESC_COMMAND :
            setDescPresence();
            break;

        case DESC_RIGHT_PANEL : 
            setDescCommand();
            break;
            
        case DESC_ARC :
            setDescRightPanel();
            break;

        case DESC_PRESENCE : 
            setDescLeftPanel();
            break;

        case DESC_LEFT_PANEL :                
            toggleTutorial();
            break;
    }
}

/*******************************************************************************
* Function Name: toggleTutorial
********************************************************************************
* Summary:
*  Function to start or end tutorial.
*
* Parameters:
*  void
*
* Return:
*  void
*
*******************************************************************************/
void toggleTutorial(){
    if(current_settings.isTutorial)
    {
        setDescArc();
        descHideAll();
        update_fan_mode(dev_fan_mode);
    }
    else 
    {
        current_settings.isTutorial = true;
        device_settings_t settings = { 0 };
        get_current_device_setting(&settings);
        app_eeprom_write(&settings);
        stop_fan_anim();
        lv_obj_clear_flag(ui_descriptionPanel, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(ui_arrow, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(ui_skipTutorial, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(ui_startTutorial, "  End Tutorial  ");
    }
}

/*******************************************************************************
* Function Name: switchDescMessagecb
********************************************************************************
* Summary:
*  Timer callback to switch tutorial description message.
*
* Parameters:
*  timer - Pointer to the LVGL timer.
*
* Return:
*  void
*
*******************************************************************************/
void switchDescMessagecb(lv_timer_t * timer){
    if(current_widget == DESC_COMMAND)
    {
        if(switchTimer) 
        {
            lv_label_set_text(ui_descriptionLabelSecondary, "  Press user Button and\n  speak voice command"); 
            lv_obj_clear_flag(ui_arrowSecondary, LV_OBJ_FLAG_HIDDEN);
            switchTimer = false;
        }
        else 
        {
            lv_label_set_text(ui_descriptionLabelSecondary, " Say \"Ok Thermostat\" to\n activate voice command"); 
            lv_obj_add_flag(ui_arrowSecondary, LV_OBJ_FLAG_HIDDEN);
            switchTimer = true;
        }
    }
    if(current_widget == DESC_PRESENCE)
    {
        if(switchTimer) 
        {
            lv_label_set_text(ui_descriptionLabel, "  Move Away to test\n  Absence Detection"); 
            switchTimer = false;
        }
        else 
        {
            lv_label_set_text(ui_descriptionLabel, "  Move closer to test\n Presence Detection"); 
            switchTimer = true;
        }
    }
}


/*******************************************************************************
* Function Name: sync_ui_via_rtc
********************************************************************************
* Summary:
*  Synchronizes the UI date/time display using the hardware RTC.
*
* Parameters:
*  void
*
* Return:
*  void
*
*******************************************************************************/
void sync_ui_via_rtc(void)
{
    rx_datetime.year   = now.year + 2000;
    rx_datetime.month  = now.month;
    rx_datetime.day    = now.day;
    rx_datetime.hour   = now.hours;
    rx_datetime.minute = now.minutes;
    rx_datetime.second = now.seconds;

    set_date_time_rtc(&rx_datetime);
    printf("Sync via RTC\n");
}

/*******************************************************************************
* Function Name: sync_ui_via_http
********************************************************************************
* Summary:
*  Synchronizes the UI date/time display using HTTP-fetched data.
*
* Parameters:
*  year - Year value.
*  month - Month value.
*  day - Day value.
*  hour - Hour value.
*  minute - Minute value.
*  second - Second value.
*
* Return:
*  void
*
*******************************************************************************/
void sync_ui_via_http(int year, int month, int day, int hour, int minute, int second)
{
    rx_datetime.year   = year;
    rx_datetime.month  = month;
    rx_datetime.day    = day;
    rx_datetime.hour   = hour;
    rx_datetime.minute = minute;
    rx_datetime.second = second;

    set_date_time_rtc(&rx_datetime);
    printf("Sync via HTTPs\n");

    (void)rtc_set_time_safe_from_http(year, month, day, hour, minute, second);
}

/*******************************************************************************
* Function Name: sync_ui_via_ble_or_mqtt
********************************************************************************
* Summary:
*  Synchronizes the UI date/time display using BLE or MQTT data.
*
* Parameters:
*  rx_datetime - DateTime structure with received date/time.
*
* Return:
*  void
*
*******************************************************************************/
void sync_ui_via_ble_or_mqtt(DateTime rx_datetime)
{
    set_date_time_rtc(&rx_datetime);
    printf("Sync via BLE or MQTT\n");

    (void)rtc_set_time_safe_from_http(rx_datetime.year, rx_datetime.month, rx_datetime.day, rx_datetime.hour, rx_datetime.minute, rx_datetime.second);
}

/*******************************************************************************
* Function Name: update_date_via_http
********************************************************************************
* Summary:
*  Updates the date and time via HTTP response data.
*
* Parameters:
*  void
*
* Return:
*  void
*
*******************************************************************************/
void update_date_via_http(void)
{
    if (strnlen(year_sync_value, sizeof(year_sync_value)) > 0 &&
        strnlen(month_sync_value, sizeof(month_sync_value)) > 0 &&
        strnlen(date_sync_value, sizeof(date_sync_value)) > 0 &&
        strnlen(hour_sync_value, sizeof(hour_sync_value)) > 0 &&
        strnlen(minute_sync_value, sizeof(minute_sync_value)) > 0 &&
        strnlen(second_sync_value, sizeof(second_sync_value)) > 0)
    {
        /* Parse HTTP time */
        int year   = atoi(year_sync_value); /* e.g., 2025 */
        int month  = atoi(month_sync_value); /* 1..12 */
        int day    = atoi(date_sync_value); /* 1..31 */
        int hour   = atoi(hour_sync_value); /* 0..23 */
        int minute = atoi(minute_sync_value); /* 0..59 */
        int second = atoi(second_sync_value); /* 0..59 */

        /* HTTP is newer -> update UI and also program external RTC */
        sync_ui_via_http(year, month, day, hour, minute, second);

        enqueue_notification(NOTIFY_HTTP_SYNC, NOTIF_SUCCESS, DEV_ST_SYNCED_HTTP);

        /* Clear buffers */
        hour_sync_value[0]   = '\0';
        minute_sync_value[0] = '\0';
        second_sync_value[0] = '\0';
        date_sync_value[0]   = '\0';
        month_sync_value[0]  = '\0';
        year_sync_value[0]   = '\0';
        
        /* Clear flag to allow handle_time_update to work again */
        is_http_fetching = false;

        /* Disable animation for future fetches */
        is_first_http_fetch = false;
    }
}

/*******************************************************************************
* Function Name: handle_http_factory_reset
********************************************************************************
* Summary:
*  Resets back to defalut as part of factory reset handling.
*
* Parameters:
*  void
*
* Return:
*  void
*
*******************************************************************************/
void handle_http_factory_reset()
{
    /* Location reset */
    if(lv_obj_has_state(ui_tempunitswitch, LV_STATE_CHECKED))
    {
        lv_label_set_text(ui_OutdoorTempLabel, "52°F");
    }
    else
    {
        lv_label_set_text(ui_OutdoorTempLabel, "11°c");
    }
    lv_label_set_text(ui_LocationLabel, "Nuremberg");

    /* Time reset */
    sync_ui_via_rtc();

    /* Clear stale HTTP sync buffers to prevent update_date_via_http()
     * from firing with leftover data and prematurely clearing is_first_http_fetch */
    hour_sync_value[0]   = '\0';
    minute_sync_value[0] = '\0';
    second_sync_value[0] = '\0';
    date_sync_value[0]   = '\0';
    month_sync_value[0]  = '\0';
    year_sync_value[0]   = '\0';

    /* Reset HTTP fetch flags so the next WiFi connection triggers animation again */
    is_http_fetching = false;
    is_first_http_fetch = true;
}

/* [] END OF FILE */
