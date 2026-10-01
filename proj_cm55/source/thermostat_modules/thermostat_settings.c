/*******************************************************************************
 * File Name:  thermostat_settings.c
 *
 * Description: Thermostat device settings and notifications.
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
#include "app_gfx_disp.h"
#include "lvgl.h"

/*******************************************************************************
 *                              CONSTANTS
 ******************************************************************************/
#define NOTIFICATION_TIMEOUT_MS     (1500U)
#define MAX_NOTIF_QUEUE     (6U)

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

static notification_data_t notif_queue[MAX_NOTIF_QUEUE];
static int notif_head = 0;
static int notif_tail = 0;
static bool notif_showing = false;
static lv_timer_t *notif_timer = NULL;

/* Notification audio type */
static audio_type_t notify_audio_type = AUDIO_ERROR;

static void show_next_notification(void);

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
        lv_anim_del(ui_notificationsetting, NULL);
        lv_anim_start(&a);
        notif_showing = true;
    }
    
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, ui_notification);
    lv_anim_set_values(&a, -297, -197);
    lv_anim_set_time(&a, 300);
    lv_anim_set_exec_cb(&a, (lv_anim_exec_xcb_t) lv_obj_set_y);
    lv_anim_del(ui_notification, NULL);
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
                lv_label_set_text(ui_notificationlabel, "Failed to sync Location");
                lv_label_set_text(ui_notificationlabelsetting, "Failed to sync Location");
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
                lv_label_set_text(ui_notificationlabel, "Synced Location");
                lv_label_set_text(ui_notificationlabelsetting, "Synced Location");
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
            return;
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
    else
    {
        LOG_ERROR(CYLF_DEF, "Invalid temperature unit: %d\n", (int) unit);
        return;
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

