/*******************************************************************************
 * File Name:  thermostat_events.c
 *
 * Description: Thermostat event handlers and device state management.
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
#include "app_gfx_disp.h"
#include "lvgl.h"

/*******************************************************************************
 *                              CONSTANTS
 ******************************************************************************/
#define CM55_APP_DELAY_MS           (50U)

#define RH_THRESHOLD                                        (55)
#define LIMIT_RH                                            (0)
#define MAX_RH                                              (100)
#define INITIAL_BRIGHTNESS_LEVEL             (100U)
/*******************************************************************************
 *                             GLOBAL VARIABLES
 ******************************************************************************/
bool is_mic_clicked = false;
volatile bool popup_overlay_visible = false;
volatile application_state_t app_state = APP_ST_ACTIVE;
device_state_t dev_info;

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

volatile bool wifi_popup_state = false;
volatile bool ble_pairing_popup_visible = false;
bool voice_popup_state = false;
bool fw_update_timeout = false;
bool is_new_fw_available = false;
bool switchTimer = true;
tutorial_descriptions_t current_widget = DESC_ARC;
lv_timer_t *tutorialTimer;

/* Presence state (shared with connectivity inactivity timer) */
bool person_detected = false;

/*******************************************************************************
 *                             STATIC VARIABLES
 ******************************************************************************/

static uint8_t person_count = 0;

uint8_t brightness_level = INITIAL_BRIGHTNESS_LEVEL;
audio_level_t audio_level = AUDIO_MED;

/*******************************************************************************
 *                              FUNCTION DEFINITIONS
 ******************************************************************************/

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
* Function Name: http_sync_complete
********************************************************************************
* Summary:
*  Called when the location sync is received. Shows the sync notification on
*  the first fetch.
*
* Parameters:
*  void
*
* Return:
*  void
*
*******************************************************************************/
void http_sync_complete(void)
{
    if (is_first_http_fetch)
    {
        enqueue_notification(NOTIFY_HTTP_SYNC, NOTIF_SUCCESS, DEV_ST_SYNCED_HTTP);
    }
    is_first_http_fetch = false;
}

/*******************************************************************************
* Function Name: http_sync_failed
********************************************************************************
* Summary:
*  Called when the HTTP fetch fails. On the first fetch, replaces the dashes
*  with the last known (or default) values and shows the failure notification.
*
* Parameters:
*  void
*
* Return:
*  void
*
*******************************************************************************/
void http_sync_failed(void)
{
    if (is_first_http_fetch)
    {
        lv_label_set_text(ui_LocationLabel,
                (location_sync_value[0] != '\0') ? location_sync_value : "Nuremberg");
        enqueue_notification(NOTIFY_HTTP_SYNC, NOTIF_FAIL, DEV_ST_SYNCED_HTTP);
    }
    is_first_http_fetch = false;
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
    location_sync_value[0] = '\0';

    /* Time reset */
    sync_ui_via_rtc();

    /* Keep the default location visible until a new city is fetched */
    is_first_http_fetch = false;
    
}



