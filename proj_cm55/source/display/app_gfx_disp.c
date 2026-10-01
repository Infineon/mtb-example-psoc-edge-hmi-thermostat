/*******************************************************************************
 * File Name:   app_gfx_disp.c
 *
 * Description: This source file contains the gfx task routine for
 *              application running on CM55 CPU.
 *
 * Related Document: See README.md
 *
********************************************************************************
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
#include <string.h>

#include "vg_lite.h"
#include "vg_lite_platform.h"

#include "cy_time.h"
#include "FreeRTOS.h"
#include "semphr.h"
#include "mtb_display_st7701s.h"
#include "lv_qrcode.h"
#include "lv_port_disp.h"
#include "lv_port_indev.h"
#include "demos/lv_demos.h"
#include "ui/ui.h"
#include "app_common.h"
#include "thermostat_events.h"
#include "comm_manager.h"
#include "app_eeprom.h"
#include "app_sensor.h"
#include "app_speaker.h"

/* Application related includes */
#include "app_gfx_disp.h"
#include "app_voice_control.h"
#include "voice_assistant.h"
#include "app_rtc.h"

/*******************************************************************************
 *                                MACROS
 *****************************************************************************/
#define GPU_INT_PRIORITY                    (3U)
#define DC_INT_PRIORITY                     (3U)

#define APP_BUFFER_COUNT                    (2U)
/* 64 KB */
#define DEFAULT_GPU_CMD_BUFFER_SIZE         ((64U) * (1024U))

#define DISP_H                              (480U)
#define DISP_W                              (512U)
#define GPU_TESSELLATION_BUFFER_SIZE        ((DISP_H) * 128U)

#define VGLITE_HEAP_SIZE                    (((DEFAULT_GPU_CMD_BUFFER_SIZE) * \
                                              (APP_BUFFER_COUNT)) + \
                                             ((GPU_TESSELLATION_BUFFER_SIZE) * \
                                              (APP_BUFFER_COUNT)))

#define GPU_MEM_BASE                        (0x0U)
#define VG_PARAMS_POS                       (0UL)

#define TARGET_NUM_FRAMES                   (40U)
#define REBOOT_DELAY_MS                     (5000U)
#define FPS_MEASUREMENT_PERIOD_MS           (500U)
#define DISPLAY_SETTLED_TIME_MS             (500U)

#define IDLE_YEAR                           (2000U)

/*******************************************************************************
 *                              GLOBAL VARIABLES
 *****************************************************************************/
/* Heap memory for VGLite to allocate memory for buffers, command, and
 * tessellation buffers 
 */
CY_SECTION(".cy_gpu_buf") uint8_t contiguous_mem[VGLITE_HEAP_SIZE] = { 0xFF };

volatile void *vglite_heap_base = &contiguous_mem;

static bool is_need_to_reboot = false;
static volatile bool touch_detected = false;
/* DC IRQ Config */
cy_stc_sysint_t dc_irq_cfg =
{
    .intrSrc      = GFXSS_DC_IRQ,
    .intrPriority = DC_INT_PRIORITY
};

/* GPU IRQ Config */
cy_stc_sysint_t gpu_irq_cfg =
{
    .intrSrc      = GFXSS_GPU_IRQ,
    .intrPriority = GPU_INT_PRIORITY
};

mtb_display_st7701s_backlight_config_t st7701s_backlight_cfg =
{
    .bl_port    = CYBSP_DISP_BACKLIGHT_PWM_PORT,
    .bl_pin     = CYBSP_DISP_BACKLIGHT_PWM_PIN,
    .pwm_hw     = CYBSP_PWM_DISP_BACKLIGHT_HW,
    .pwm_num    = CYBSP_PWM_DISP_BACKLIGHT_NUM,
    .pwm_config = &CYBSP_PWM_DISP_BACKLIGHT_config
};

/* Location and weather variables */
char weather_sync_value[32] = "11";
char location_sync_value[32];
char weather_code_sync_value[8];
DateTime rx_datetime;
rtc_time_t now;

uint32_t idle_percent = 0;
uint32_t fps = 0;

/* Flag to show dashes only on first HTTP fetch */
bool volatile is_first_http_fetch = true;

/*******************************************************************************
 *                              FUNCTION DEFINITIONS
 ******************************************************************************/

/*******************************************************************************
* Function Name: handle_eeprom_write
********************************************************************************
* Summary:
*  Handles the writing of device settings to EEPROM.
*
* Parameters:
*  void
*
* Return:
*  void
*
*******************************************************************************/
static void handle_eeprom_write(void)
{
    /** Check if the EEPROM write setting flag is enabled. */
    if (eeprom_wr_setting)
    {
        /** A temporary structure to hold current device settings. */
        static device_settings_t settings;
        memset(&settings, 0, sizeof(settings));

        /** Get the current settings from the device's state. */
        get_current_device_setting(&settings);

        /** Write the settings to the application EEPROM. */
        app_eeprom_write(&settings);

        /* Clear flag */
        eeprom_wr_setting = false;
    }
}

/*******************************************************************************
* Function Name: handle_touch_event
********************************************************************************
* Summary:
*  Handles touch events detected on the UI.
*
* Parameters:
*  void
*
* Return:
*  void
*
*******************************************************************************/
static void handle_touch_event(void)
{
    /** Check if a touch event has been detected. */
    if (touch_detected)
    {
        /** Reset the touch detection flag. */
        touch_detected = false;

        /** Restart the inactivity timer to prevent device idle state. */
        start_inactivity_timer();
    }
}

/*******************************************************************************
* Function Name: handle_time_update
********************************************************************************
* Summary:
*  Updates the UI with the current time and date.
*
* Parameters:
*  void
*
* Return:
*  void
*
*******************************************************************************/
static void handle_time_update(void)
{

    /** Check if the timestamp update flag is set. */
    if (update_timestamp)
    {
        /** Clear the flag to prevent redundant updates. */
        update_timestamp = false;
        char time_str[6];
        char date_str[20];

        /** Update the hour labels for the main screen and low-power screen. */
        snprintf(time_str, sizeof(time_str), "%02u :", ui_current_time.hour);
        lv_label_set_text(ui_TimeHactive, time_str);

        snprintf(time_str, sizeof(time_str), "%02u :", ui_current_time.hour);
        lv_label_set_text(ui_TimeHLP, time_str);

        /** Update the minute labels. */
        snprintf(time_str, sizeof(time_str), "%02u", ui_current_time.min);
        lv_label_set_text(ui_TimeMactive, time_str);

        snprintf(time_str, sizeof(time_str), " %02u :", ui_current_time.min);
        lv_label_set_text(ui_TimeMLP, time_str);

        /** Update the second label on the low-power screen. */
        snprintf(time_str, sizeof(time_str), " %02u", ui_current_time.sec);
        lv_label_set_text(ui_TimeSLP, time_str);

        /** Use the standard C library for date formatting. */
        struct tm date_time = {0};
        uint32_t year = IDLE_YEAR + ui_current_time.year;

        /** Populate the tm structure with current RTC values. */
        date_time.tm_sec = ui_current_time.sec;
        date_time.tm_min = ui_current_time.min;
        date_time.tm_hour = ui_current_time.hour;
        date_time.tm_mday = ui_current_time.date;
        date_time.tm_mon = ui_current_time.month - 1u;
        date_time.tm_year = year - 1900;
        date_time.tm_wday = ui_current_time.dayOfWeek - 1u;

        /** Use strftime to format the date string. */
        strftime(date_str, sizeof(date_str), "%a %d %b", &date_time);

        /** Update the date labels. */
        lv_label_set_text(ui_Dateactive, date_str);
        lv_label_set_text(ui_DateLP, date_str);

        cy_rslt_t result = rtc_get_time(&now);
        if (CY_RSLT_SUCCESS != result) {
            printf("RTC Read time failed: 0x%08lx\r\n", (unsigned long)result);
        }

    }
}

/*******************************************************************************
* Function Name: handle_sensor_update
********************************************************************************
* Summary:
*  Handles updates from the system's sensor and updates the UI.
*
* Parameters:
*  void
*
* Return:
*  void
*
*******************************************************************************/
static void handle_sensor_update(void)
{
    static bool humidity_alert_active = false;   /* one alert per overshoot */
    static bool ventilation_active     = false;  /* true only if we forced mode to auto */
    static fan_speed_t       previous_fan_speed = FAN_MED;

    if (sensor_data_available)
    {
        bool overshoot = check_humidity_overshoot(rh_pct);
        thermostat_mode_t mode = get_current_device_mode();

        if (overshoot)
        {
            /* High humidity alert in every mode (edge-triggered) */
            if (!humidity_alert_active)
            {
                LOG_INFO(CYLF_DEF, "High Humidity Alert: %d %% \n", rh_pct);
                enqueue_notification(NOTIFY_HUM_WARNING, NOTIF_FAIL, HIGH_HUMIDITY);
                humidity_alert_active = true;
            }

            /* Improve ventilation only in AUTO */
            if (mode == MODE_AUTO)
            {
                if (!ventilation_active)
                {
                    fan_speed_t current_fan_speed = get_current_fan_mode();

                    if (current_fan_speed != FAN_HIGH)
                    {
                        previous_fan_speed = current_fan_speed;

                        current_settings.thermostat_setting.fan_mode = FAN_HIGH;
                        update_fan_mode(FAN_HIGH);

                        update_thermostat_mode_timer();
                        update_current_device_setting();

                        LOG_INFO(CYLF_DEF, "Improving ventilation to reduce humidity\n");
                        enqueue_notification(NOTIFY_HUM_VENTI, NOTIF_SUCCESS, HIGH_HUMIDITY);

                        ventilation_active = true;
                    }
                }
            }
            else
            {
                /* If we had forced ventilation in AUTO and user left AUTO, revert silently */
                if (ventilation_active)
                {
                    current_settings.thermostat_setting.fan_mode = previous_fan_speed;
                    update_fan_mode(previous_fan_speed);

                    update_thermostat_mode_timer();
                    update_current_device_setting();

                    ventilation_active = false;
                }
            }
        }
        else
        {
            /* End of overshoot: clear alert state */
            if (humidity_alert_active)
            {
                humidity_alert_active = false;
            }

            /* Revert ventilation only if we had forced it, and send "Improved" only in AUTO */
            if (ventilation_active)
            {
                current_settings.thermostat_setting.fan_mode = previous_fan_speed;
                update_fan_mode(previous_fan_speed);
                
                update_thermostat_mode_timer();
                update_current_device_setting();

                if (mode == MODE_AUTO)
                {
                    LOG_INFO(CYLF_DEF, "Improved humidity\n");
                    enqueue_notification(NOTIFY_HUM_VENTI_IMP, NOTIF_SUCCESS, LOW_HUMIDITY);
                }
                /* Outside AUTO: revert silently (no notification) */

                ventilation_active = false;
            }
        }

        /* Update UI and clear data flag */
        update_humidity_data_ui(rh_pct);
        sensor_data_available = false;
    }
}

/*******************************************************************************
* Function Name: handle_system_event
********************************************************************************
* Summary:
*  Processes all incoming IPC messages and routes them to appropriate handlers.
*
* Parameters:
*  void
*
* Return:
*  void
*
*******************************************************************************/
static void handle_system_event(void)
{
    ipc_msg_cm33_to_cm55_t local_msg;

    /* Drain every message queued since the last call. The FreeRTOS queue holds
     * each message until this loop reads it, so a burst arriving faster than
     * the loop iterates is preserved in order rather than overwriting one
     * another. */
    while (xQueueReceive(cm55_ipc_rx_queue, &local_msg, 0) == pdPASS)
    {
        switch (local_msg.cmd)
        {
            case IPC_CMD_UPDATE_PRESENCE_STATUS:
                #if defined (RADAR_DETECTION)
                /** Update UI based on presence detection status. */
                stop_active_state_timer();
                if ((presence_status_t) local_msg.data == PRESENCE_DETECTED)
                {
                    /** Switch to Active screen and display presence status. */
                    switch_to_active_screen();
                    update_presence_detection(PRESENCE_DETECTED);
                    lv_obj_set_style_bg_image_opa(ui_presencelbl, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
                }
                else if ((presence_status_t) local_msg.data == ABSENCE_DETECTED)
                {
                    /** Update absence status. */
                    update_presence_detection(ABSENCE_DETECTED);
                    lv_obj_set_style_bg_image_opa(ui_presencelbl, 150, LV_PART_MAIN | LV_STATE_DEFAULT);
                }
                #endif
                break;

            case IPC_CMD_SET_UID:
                /** Set the device's unique ID from the IPC message. */
                LOG_INFO(CYLF_DEF, "Rx UID: %.*s\n", (int) sizeof(local_msg.unique_id), local_msg.unique_id);
                memcpy(device_unique_id, local_msg.unique_id, sizeof(device_unique_id) - 1);
                device_unique_id[sizeof(device_unique_id) - 1] = '\0';
                lv_label_set_text(ui_deviceIDlabel, device_unique_id);
                update_device_config_ipc();
                break;

            case IPC_CMD_SET_DATE_TIME:
            {
                /** Switch to Active screen and display presence status. */
                switch_to_active_screen();

                /** Set the device's date and time from the IPC message. */
                DateTime rx_datetime;
                memset(&rx_datetime, 0, sizeof(rx_datetime));
                memcpy(&rx_datetime, &(local_msg.datetime), sizeof(DateTime));
                sync_ui_via_ble_or_mqtt(rx_datetime);
                break;
            }

            case IPC_CMD_DEVICE_CONFIG:
                /** Update the device configuration via IPC. */
                update_device_config_ipc();
                break;

            case IPC_CMD_SET_DISPLAY_BRIGHTNESS:
                /** Switch to Active screen and display presence status. */
                switch_to_active_screen();

                /** Set the display brightness. */
                update_display_brightness(local_msg.data);
                break;

            case IPC_CMD_SET_AUDIO_LEVEL:
                /** Switch to Active screen and display presence status. */
                switch_to_active_screen();

                /** Set the audio output level. */
                update_thermostat_volume((audio_level_t) local_msg.data);
                break;

            case IPC_CMD_SET_TEMP_UNIT:
                /** Switch to Active screen and display presence status. */
                switch_to_active_screen();

                /** Set the temperature unit and update IPC. */
                update_system_unit((temp_unit_t) local_msg.data);
                update_device_config_ipc();
                break;

            case IPC_CMD_UPDATE_CONN_STATE:
            {
                /** Update the device's connection state based on the IPC message. */
                switch ((device_connection_state_t) local_msg.data)
                {
                    case DEV_ST_BLE_ADVERTISING:
                        request_uid_ipc();
                        /* fallthrough */
                    case DEV_ST_WIFI_CONNECTING:
                        /* Dont turn off screen if device is not provisioned */
                        if(!is_device_provisioned)
                        {
                            stop_active_state_timer();
                        }
                        update_device_connection_state((device_connection_state_t) local_msg.data);
                        break;

                    case DEV_ST_UNPROVISIONED:
                    case DEV_ST_CLOUD_DISCONNECTED:
                    case DEV_ST_WIFI_DISCONNECTED:
                        update_device_connection_state((device_connection_state_t) local_msg.data);
                        start_inactivity_timer();
                        break;

                    case DEV_ST_CLOUD_CONNECTING:
                        update_device_connection_state((device_connection_state_t) local_msg.data);
                        break;

                    case DEV_ST_WIFI_CONNECTED:
                        update_device_connection_state((device_connection_state_t) local_msg.data);
                        switch_to_active_screen();
                        break;

                    case DEV_ST_BLE_CONNECTED:
                        update_device_connection_state((device_connection_state_t) local_msg.data);
                        switch_to_active_screen();
                        start_inactivity_timer();
                        break;

                    case DEV_ST_CLOUD_CONNECTED:
                        update_device_connection_state((device_connection_state_t) local_msg.data);
                        switch_to_active_screen();
                        request_uid_ipc();
                        start_inactivity_timer();
                        break;

                    default:
                        break;
                }
                break;
            }
            case IPC_CMD_SET_FAN_SPEED:
            {
                /** Switch to Active screen and display presence status. */
                switch_to_active_screen();

                refresh_fan_mode_ui((fan_speed_t) local_msg.data);
                current_settings.thermostat_setting.fan_mode = (fan_speed_t) local_msg.data;
                update_current_device_setting();
                break;
            }
            case IPC_CMD_SET_THERMOSTAT_MODE:
            {
                /** Switch to Active screen and display presence status. */
                switch_to_active_screen();

                /** Set the thermostat mode and update settings. */
                update_thermostat_mode((thermostat_mode_t) local_msg.data);
                current_settings.thermostat_setting.mode = (thermostat_mode_t) local_msg.data;
                current_settings.thermostat_setting.fan_mode = get_current_fan_mode();
                update_thermostat_mode_timer();
                update_current_device_setting();
                break;
            }
            case IPC_CMD_SET_TARGET_TEMP:
            {
                /** Switch to Active screen and display presence status. */
                switch_to_active_screen();

                /** Update the target temperature. */
                update_device_temp((uint8_t) local_msg.data);
                break;
            }
            case IPC_CMD_CURRENT_EVENT:
            {
                /** Display the BLE pairing window with a code. */
                char pin[7] = { 0 };
                snprintf(pin, sizeof(pin), "%06lu", (unsigned long) local_msg.data);
                if (local_msg.data > 1)
                {
                    /** Switch to Active screen and display presence status. */
                    switch_to_active_screen();
                    stop_active_state_timer();
                    display_ble_pairing_window(0, pin);
                }
                else
                {
                    display_ble_pairing_window(1, pin);
                }
                break;
            }

            case IPC_CMD_GET_CURRENT_TEMP:
                /** Get and update the current temperature via IPC. */
                update_current_temp_ipc(get_current_temperature());
                break;

            case IPC_CMD_GET_DISPLAY_BRIGHTNESS:
                /** Get and update the brightness via IPC. */
                update_brightness_ipc(get_current_brigthness());
                break;

            case IPC_CMD_GET_FAN_SPEED:
                /** Get and update the fan speed via IPC. */
                update_fan_speed_ipc(get_current_fan_mode());
                break;

            case IPC_CMD_GET_THERMOSTAT_MODE:
                /** Get and update the thermostat mode via IPC. */
                update_device_mode_ipc(get_current_device_mode());
                break;

            case IPC_CMD_UPDATE_PROVISION_STATE:
                /** Update the provisioned state of the device. */
                is_device_provisioned = local_msg.data;
                break;

            case IPC_CMD_HIDE_CONN_POPUP:
                /** Mobile app sent data over BLE -> dismiss connectivity/pairing popups. */
                dismiss_connectivity_popups();
                break;

            case IPC_CMD_DISABLE_TOUCH:
                LOG_INFO(CYLF_DEF, "Disable the touch\n");
                Cy_SCB_I2C_DeInit(CYBSP_I2C_DISPLAY_CONTROLLER_HW);
                break;

            case IPC_CMD_NO_INTERNET_NOTIFY:
                _ui_screen_change(&ui_ActiveScreen, LV_SCR_LOAD_ANIM_MOVE_LEFT, 230, 0, &ui_ActiveScreen_screen_init);
                enqueue_notification(NOTIFY_NETWORK_STATUS, NOTIF_FAIL, DEV_ST_NO_INTERNET);
                break;

            case IPC_CMD_UPDATE_CURRENT_SCREEN:
            {
                switch((screen_id_t)local_msg.data)
                {
                case SCREEN_FW:
                    _ui_screen_change(&ui_FWUpdateScreen, LV_SCR_LOAD_ANIM_NONE, 0, 0, &ui_FWUpdateScreen_screen_init);
                    LOG_INFO(CYLF_DEF, "Displaying Firmware Screen\n");
                    break;

                default:
                    printf("Unknown Screen\n");
                    break;
                }
                break;
            }

            case IPC_CMD_WEATHER_SYNC:
                LOG_INFO(CYLF_DEF, "\nWeather data received in CM55: %.*s\n", (int) sizeof(local_msg.char_value), local_msg.char_value);

                /* Clear and copy the weather data */
                memset(weather_sync_value, 0, sizeof(weather_sync_value));
                strncpy(weather_sync_value, local_msg.char_value, sizeof(weather_sync_value) - 1);
                weather_sync_value[sizeof(weather_sync_value) - 1] = '\0';
                lv_label_set_text_fmt(ui_OutdoorTempLabel, "%d°c", atoi(weather_sync_value));
                break;

            case IPC_CMD_LOCATION_SYNC:
                LOG_INFO(CYLF_DEF, "\nLocation data received in CM55: %.*s\n", (int) sizeof(local_msg.char_value), local_msg.char_value);

                /* Clear and copy the location data */
                memset(location_sync_value, 0, sizeof(location_sync_value));
                strncpy(location_sync_value, local_msg.char_value, sizeof(location_sync_value) - 1);
                location_sync_value[sizeof(location_sync_value) - 1] = '\0';
                lv_label_set_text(ui_LocationLabel, location_sync_value);
                http_sync_complete();
                break;

            case IPC_CMD_WEATHER_CODE_SYNC:
                LOG_INFO(CYLF_DEF, "Weather code received in CM55: %.*s\n", (int) sizeof(local_msg.char_value), local_msg.char_value);

                memset(weather_code_sync_value, 0, sizeof(weather_code_sync_value));
                strncpy(weather_code_sync_value, local_msg.char_value, sizeof(weather_code_sync_value) - 1);
                weather_code_sync_value[sizeof(weather_code_sync_value) - 1] = '\0';

                LOG_INFO(CYLF_DEF, "Weather code stored: %s\n", weather_code_sync_value);
                update_weather_info_via_http();
                break;

            case IPC_CMD_HTTP_FETCHING_START:           
                if (is_first_http_fetch)
                {
                    
                    /* Set location to dashes  */
                    lv_label_set_text(ui_LocationLabel, "- - -");
                }
                break;

            case IPC_CMD_HTTP_FETCH_FAILED:
                http_sync_failed();
                break;

            default:
                break;
        }
    }

    /* Log once whenever cm55_ipc_rx_drop_count changes, so a dropped message
     * (rx queue full at enqueue time) is reported rather than lost silently. */
    {
        static uint32_t last_reported_drop_count = 0;
        uint32_t current_drop_count = cm55_ipc_rx_drop_count;

        if (current_drop_count != last_reported_drop_count)
        {
            LOG_ERROR(CYLF_DEF, "[IPC] cm55_ipc_rx_queue FULL - dropped %lu message(s) so far\n",
                    (unsigned long) current_drop_count);
            last_reported_drop_count = current_drop_count;
        }
    }
}

/*******************************************************************************
* Function Name: calculate_fps
********************************************************************************
* Summary:  
*  This function calculates the frames per second (FPS) based on the number of
*  frames rendered and the elapsed time since the last calculation.
*  It resets the frame count and start time after reaching the target number of
*  frames.

* Parameters:
*  void   
*  
* Return:
*  None
*******************************************************************************/
void calculate_fps(void)
{
    static uint32_t last_report_time_ms = 0;
    static uint32_t frame_count = 0;
    static bool initialized = false;
    
    if (!initialized)
    {
        last_report_time_ms = get_time_ms();
        initialized = true;
        return;
    }
    
    /* Increment frame counter */
    frame_count++;
    
    uint32_t current_time_ms = get_time_ms();
    uint32_t time_since_last_report = current_time_ms - last_report_time_ms;
    
    if (time_since_last_report >= FPS_MEASUREMENT_PERIOD_MS)
    {
        fps = (frame_count * 1000) / time_since_last_report;
        
        /* Get CPU usage */
        idle_percent = calculate_idle_percentage();
        uint32_t cpu_usage = 100 - idle_percent;

        if(fps<= TARGET_NUM_FRAMES){
            /* Update all UI labels */
            lv_label_set_text_fmt(ui_FPSlabel, "FPS: %u CPU: %u%%", 
                                (unsigned int)fps, (unsigned int)cpu_usage);
            lv_label_set_text_fmt(ui_FPSlabel1, "FPS: %u CPU: %u%%", 
                                (unsigned int)fps, (unsigned int)cpu_usage);
            lv_label_set_text_fmt(ui_FPSlabel2, "FPS: %u CPU: %u%%", 
                                (unsigned int)fps, (unsigned int)cpu_usage);
            lv_label_set_text_fmt(ui_FPSlabel3, "FPS: %u CPU: %u%%", 
                                (unsigned int)fps, (unsigned int)cpu_usage);
            }

        frame_count = 0;
        last_report_time_ms = current_time_ms;
    }
}
/*******************************************************************************
* Function Name: dc_irq_handler
********************************************************************************
* Summary:
*  Display Controller interrupt handler which gets invoked when the DC finishes
*  utilizing the current frame buffer.
*
* Parameters:
*  void
*
* Return:
*  void
*
*******************************************************************************/
static void dc_irq_handler(void)
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;

    Cy_GFXSS_Clear_DC_Interrupt(GFXSS, &gfx_context);

    /* Notify the cm55_gfx_task */
    xTaskNotifyFromISR(rtos_cm55_gfx_task_handle, 1, eSetValueWithOverwrite, 
                       &xHigherPriorityTaskWoken);

    /* Perform a context switch if a higher-priority task was woken */
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}


/*******************************************************************************
* Function Name: gpu_irq_handler
********************************************************************************
* Summary:
*  GPU interrupt handler which gets invoked when the GPU finishes composing
*  a frame.
*
* Parameters:
*  void
*
* Return:
*  void
*
*******************************************************************************/
static void gpu_irq_handler(void)
{
    Cy_GFXSS_Clear_GPU_Interrupt(GFXSS, &gfx_context);
    vg_lite_IRQHandler();
}

/*******************************************************************************
* Function Name: cm55_gfx_task
********************************************************************************
* Summary:
*   This is the FreeRTOS task callback function.
*   It initializes:  
*       - GFX subsystem.
*       - Configure the DC, GPU interrupts.
*       - Initialize I2C interface to be used for touch as well as 4-inch display
*         drivers. 
*       - Initializes the display panel selected through Makefile component and
*         vglite driver.
*       - Allocates vglite memory.
*       - Configures LVGL, low level display and touch driver.
*       - Finally invokes the UI application.
*
* Parameters:
*  void *arg: Pointer to the argument passed to the task (not used)
*
* Return:
*  void
*
*******************************************************************************/
void cm55_gfx_task(void *arg)
{
    CY_UNUSED_PARAMETER(arg);

    uint32_t time_till_next = 0;
    static bool boot_config = true;
    static bool rtc_time_ui_synced = false;
    cy_en_sysint_status_t sysint_status = CY_SYSINT_SUCCESS;
    cy_en_gfx_status_t gfx_status = CY_GFX_SUCCESS;
    vg_lite_error_t vglite_status = VG_LITE_SUCCESS;
    cy_en_mipidsi_status_t mipi_status = CY_MIPIDSI_SUCCESS;

    /* GFXSS init */
    /* Set frame buffer address to the GFXSS configuration structure */
    GFXSS_config.dc_cfg->gfx_layer_config->buffer_address    = frame_buffer1;
    GFXSS_config.dc_cfg->gfx_layer_config->uv_buffer_address = frame_buffer1;

    /* Initialize Graphics subsystem as per the configuration */
    gfx_status = Cy_GFXSS_Init(GFXSS, &GFXSS_config, &gfx_context);

    if (CY_GFX_SUCCESS == gfx_status)
    {

        /* Initialize GFXSS DC interrupt */
        sysint_status = Cy_SysInt_Init(&dc_irq_cfg, dc_irq_handler);
 
        if (CY_SYSINT_SUCCESS != sysint_status)
        {
            LOG_ERROR(CYLF_DEF, "Error in registering DC interrupt: %d\r\n", sysint_status);
            CY_ASSERT(0);
        }

        /* Enable GFX DC interrupt in NVIC. */
        NVIC_EnableIRQ(GFXSS_DC_IRQ);

        /* Initialize GFX GPU interrupt */
        sysint_status = Cy_SysInt_Init(&gpu_irq_cfg, gpu_irq_handler);

        if (CY_SYSINT_SUCCESS != sysint_status)
        {
            LOG_ERROR(CYLF_DEF, "Error in registering GPU interrupt: %d\r\n", sysint_status);
            CY_ASSERT(0);
        }
 
        /* Enable GPU interrupt */
        Cy_GFXSS_Enable_GPU_Interrupt(GFXSS);

        /* Enable GFX GPU interrupt in NVIC. */
        NVIC_EnableIRQ(GFXSS_GPU_IRQ);

        LOG_INFO(CYLF_DEF, "[cm55_gfx_task] GFX subsystem initialized, configuring I2C\r\n");

        Cy_SCB_I2C_Enable(CYBSP_I2C_DISPLAY_CONTROLLER_HW);
        vTaskDelay(pdMS_TO_TICKS(DISPLAY_SETTLED_TIME_MS));

        /* Initialize the R4INCH display */
        mipi_status =  mtb_display_st7701s_init(GFXSS_GFXSS_MIPIDSI,&st7701s_backlight_cfg);
        if(CY_MIPIDSI_SUCCESS != mipi_status)
        {
            LOG_ERROR(CYLF_DEF, "[cm55_gfx_task] st7701s 4-inch display init failed with status = %d\r\n", mipi_status);
            CY_ASSERT(0);
        }

        mtb_display_st7701s_set_brightness(brightness_level);

        LOG_INFO(CYLF_DEF, "[cm55_gfx_task] Initializing VGLite\r\n");
        /* Allocate memory for VGLite from the vglite_heap_base */
        vg_module_parameters_t vg_params;
        vg_params.register_mem_base = (uint32_t)GFXSS_GFXSS_GPU_GCNANO;
        vg_params.gpu_mem_base[VG_PARAMS_POS] = GPU_MEM_BASE;
        vg_params.contiguous_mem_base[VG_PARAMS_POS] = vglite_heap_base;
        vg_params.contiguous_mem_size[VG_PARAMS_POS] = VGLITE_HEAP_SIZE;

        /* Initialize VGlite memory. */
        vg_lite_init_mem(&vg_params);

        /* Initialize the memory and data structures needed for VGLite draw/blit
         * functions
         */
        vglite_status = vg_lite_init((MY_DISP_HOR_RES) ,
                             (MY_DISP_VER_RES));

        if (VG_LITE_SUCCESS == vglite_status)
        {
            LOG_INFO(CYLF_DEF, "[cm55_gfx_task] VGLite initialized, initializing LVGL\r\n");
            /* Initialize LVGL library */
            lv_init();
            lv_port_disp_init();
            lv_port_indev_init();
            ui_init();
            ui_timer_init();
            LOG_INFO(CYLF_DEF, "[cm55_gfx_task] LVGL initialized successfully, entering main loop\r\n");
        }
        else
        {
            LOG_ERROR(CYLF_DEF, "vg_lite_init failed, status: %d\r\n", vglite_status);

            /* Deallocate all the resources and free up all the memory */
            vg_lite_close();
            CY_ASSERT(0);
        }
    }
    else
    {
        LOG_ERROR(CYLF_DEF, "Graphics subsystem init failed, status: %d\r\n", gfx_status);
        CY_ASSERT(0);
    }

    LOG_INFO(CYLF_DEF, "[cm55_gfx_task] Entering main event loop\r\n");

    /* Initialize I2C SCB for RTC */
    cy_rtc_controller();

    for (;;)
    {
        if(is_need_to_reboot)
        {
            LOG_ERROR(CYLF_DEF, "[cm55_gfx_task] Reboot requested, resetting system\r\n");
            vTaskDelay(REBOOT_DELAY_MS);
            NVIC_SystemReset();
        }

        /* Process sensor update event */
        handle_sensor_update();

        /* Process system IPC events */
        handle_system_event();

        /* LVGL's timer handler function, to be called periodically to handle
         * LVGL tasks.
         */
        time_till_next = lv_timer_handler();

        vTaskDelay(pdMS_TO_TICKS(time_till_next));

#if defined(USE_VOICE_ASSISTANT)
        if(bootup_done)
        {

            /* Read the user button state */
            if (check_button_pressed() || is_mic_clicked)
            {
                /** Switch to Active screen  */
                switch_to_active_screen();
    
                printf("Push to Talk Button detected! Speak a command!\r\n");
                Cy_GPIO_Set(CYBSP_LED_BLUE_PORT, CYBSP_LED_BLUE_NUM);
    
                is_mic_clicked = false;
                cur_voice_active = true;
                voice_assistant_change_state(VA_RUN_CMD);
            }
    
            ww_to_ui();
            intent_to_ui(intent_text);
        }
#endif

        if(boot_config)
        {
            load_thermostat_config(current_settings.thermostat_setting.mode);
            update_device_config_ipc();
            boot_config = false;
        }

        /* If eeprom write operation pending */
        handle_eeprom_write();

        /* If touch event detected restart the inactivity timer */
        handle_touch_event();

        app_speaker_clear();

        /* Update time on UI */
        handle_time_update();

        calculate_fps();

        /* Hide connectivity pop-up screen
         * if BLE/Cloud connected state  */
        if (hide_conn_screen)
        {
            hide_connectivity_screen();
            hide_conn_screen = false;
        }
        
        if(!rtc_time_ui_synced)
        {
            /* Sync UI time with current RTC */
            sync_ui_via_rtc();
            rtc_time_ui_synced = true;
        }
    }
}

/* [] END OF FILE */
