/*******************************************************************************
 * File Name:  thermostat_control.c
 *
 * Description: Thermostat temperature, fan and mode control.
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
#include "ui/ui.h"
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
#define ECO_MODE_TEMP_TIMER_TIMEOUT     (5000U)
#define RAPID_MODE_TEMP_TIMER_TIMEOUT       (1000U)
#define AUTO_MODE_TEMP_TIMER_TIMEOUT        (2500U)

#define ECO_MODE_DEG_DELAY_SEC              (5)
#define RAPID_MODE_DEG_DELAY_SEC            (1)
#define AUTO_MODE_DEG_DELAY_SEC             (3)

#define AUTO_MODE_TARGET_TEMP_DEFAULT_C (22U)
#define AUTO_MODE_TARGET_TEMP_DEFAULT_F CELSIUS_TO_FAHRENHEIT(AUTO_MODE_TARGET_TEMP_DEFAULT_C)

#define TEMPERATURE_ARC_START_ANGLE (120U)
#define TEMPERATURE_ARC_END_ANGLE   (420U)
#define TEMPERATURE_ARC_ANGLE_PER_STEP (18U)

/*******************************************************************************
 *                             VARIABLES
 ******************************************************************************/
char subinfo[64] = { 0 };

uint8_t current_max_temp = TEMPERATURE_DEG_C_MAX_VALUE;
uint8_t current_min_temp = TEMPERATURE_DEG_C_MIN_VALUE;

int temperature = DEFAULT_STARTUP_TEMPERATURE;
int current_temp = DEFAULT_STARTUP_TEMPERATURE;
int target_temp = DEFAULT_STARTUP_TEMPERATURE;

/* Current thermostat mode */
thermostat_mode_t dev_current_mode = MODE_ECO;

/* Current fan mode */
fan_speed_t dev_fan_mode = FAN_OFF;

/* Current selected temperature unit */
temp_unit_t dev_unit = TEMP_UNIT_CELSIUS;

static volatile uint32_t deg2sec = ECO_MODE_TEMP_TIMER_TIMEOUT;

static animation_state_t current_state = STATE_NONE;

int angle;

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

            LOG_DEBUG(CYLF_DEF, "Updated Start angle: %d, End Angle: %d\n", start_angle, end_angle);
            LOG_DEBUG(CYLF_DEF, "Updated CT: %d, TT: %d\n", current_temp, target_temp);

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

            LOG_DEBUG(CYLF_DEF, "Start angle: %d, End Angle: %d\n", start_angle, end_angle);
            LOG_DEBUG(CYLF_DEF, "CT: %d, TT: %d\n", current_temp, target_temp);

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
void refresh_fan_mode_ui(fan_speed_t mode)
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
            /* Normalise unknown mode to a safe default before it is stored. */
            LOG_ERROR(CYLF_DEF, "Invalid fan mode: %d, defaulting to OFF\n", (int) mode);
            mode = FAN_OFF;
            fan_off();
            lv_label_set_text_fmt(ui_fanmodelabel, "OFF");
            current_state = STATE_NONE;
            break;
    }

    dev_fan_mode = mode;

    if(current_settings.isTutorial){
        stop_fan_anim();
    }
}

void update_fan_mode(fan_speed_t mode)
{
    refresh_fan_mode_ui(mode);

    /* update_fan_mode() is the setpoint-change entry point and transmits the
     * new speed over IPC/BLE when connected; UI-only updates and echoes of a
     * CM33-received value go through refresh_fan_mode_ui() instead and do not
     * transmit, so a received value is never fed back as a new change. */
    if (true == is_device_connected)
    {
        update_fan_speed_ipc(dev_fan_mode);
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
        enqueue_notification(NOTIFY_TEMP_UPDATE, NOTIF_FAIL, 0);
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

            LOG_DEBUG(CYLF_DEF, "Start angle: %d, End Angle: %d\n", start_angle, end_angle);
            LOG_DEBUG(CYLF_DEF, "CT: %d, TT: %d\n", current_temp, target_temp);

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

            LOG_DEBUG(CYLF_DEF, "Start angle: %d, End Angle: %d\n", start_angle, end_angle);
            LOG_DEBUG(CYLF_DEF, "CT: %d, TT: %d\n", current_temp, target_temp);
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
        /* Visual refresh only - must not re-emit the fan setpoint over IPC. */
        refresh_fan_mode_ui(dev_fan_mode);
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

