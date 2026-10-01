/*******************************************************************************
 * File Name:   app_voice_control.c
 *
 * Description: Code for voice control using DEEPCRAFT voice assistant.
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

#include "stdlib.h"

#include "cy_pdl.h"
#include "cycfg.h"

#include "app_voice_control.h"

/* Peripherals related includes */
#include "pdm_mic_interface.h"
#include "audio_input_configuration.h"
#include "audio_conv_utils.h"

#ifdef USE_AUDIO_ENHANCEMENT
#include "audio_enhancement.h"
#endif

#ifdef PROFILER_ENABLE
#include "cy_afe_profiler.h"
#include "cy_profiler.h"
#endif /* PROFILER_ENABLE */

#include "thermostat_events.h"
#include "app_gfx_disp.h"

/*******************************************************************************
 *                                MACROS
 *****************************************************************************/
/* Debounce counter for button presses (multiply by 10 ms) */
#define BUTTON_DEBOUNCE_COUNT                   (2U)

/*******************************************************************************
 *                              GLOBAL VARIABLES
 *****************************************************************************/

#ifdef ENABLE_STEREO_INPUT_FEED
int16_t non_interleaved_audio[2*PDM_MIC_SAMPLES_COUNT] = {0};
#endif

int16_t va_cmd;

bool handle_ww_for_ui = false;
bool handle_command_for_ui = false;
bool va_cmd_present = false;

bool cur_voice_active = false;
bool pre_voice_active = false;

/* Timer handles for LED flashing */
static TimerHandle_t red_led_timer = NULL;
static TimerHandle_t green_led_timer = NULL;

/*******************************************************************************
* Function Declarations
*******************************************************************************/
void handle_VA_command(lv_timer_t * timer);
void handle_va_process_output(va_event_t event);

/*******************************************************************************
 *                          FUNCTION DEFINITIONS
 *****************************************************************************/
/*******************************************************************************
 * Function Name: red_led_flash_timeout_cb
 *******************************************************************************
 * Summary:
 * Callback function to turn off the red LED after flashing.
 *
 * Parameters:
 *  timer: Pointer to the timer that triggered this callback.
 *
 * Return:
 *  void
 *
 *******************************************************************************/
static void red_led_flash_timeout_cb(TimerHandle_t xTimer)
{
    (void)xTimer;
    Cy_GPIO_Clr(CYBSP_LED_RED_PORT, CYBSP_LED_RED_NUM);
}

/*******************************************************************************
 * Function Name: green_led_flash_timeout_cb
 *******************************************************************************
 * Summary:
 * Callback function to turn off the green LED after flashing.
 *
 * Parameters:
 *  timer: Pointer to the timer that triggered this callback.
 *
 * Return:
 *  void
 *
 *******************************************************************************/
static void green_led_flash_timeout_cb(TimerHandle_t xTimer)
{
    (void)xTimer;
    Cy_GPIO_Clr(CYBSP_LED_GREEN_PORT, CYBSP_LED_GREEN_NUM);
}

/*******************************************************************************
 * Function Name: handle_va_process_output
 *******************************************************************************
 * Summary:
 * Callback function to handle the voice assistant output events.
 *
 * Parameters:
 *  event: The voice assistant event that occurred.
 *
 * Return:
 *  void
 *
 *******************************************************************************/
void handle_va_process_output(va_event_t event)
{
    if ( event == VA_EVENT_WW_DETECTED )
    {
        cur_voice_active = true;
        handle_ww_for_ui = true;

        Cy_GPIO_Set(CYBSP_LED_BLUE_PORT, CYBSP_LED_BLUE_NUM);
    }
    else if ( event == VA_EVENT_WW_NOT_DETECTED )
    {
        cur_voice_active = false;
        
        Cy_GPIO_Clr(CYBSP_LED_BLUE_PORT, CYBSP_LED_BLUE_NUM);
    }
    else if ( event == VA_EVENT_CMD_TIMEOUT )
    {
        cur_voice_active = false;

        Cy_GPIO_Clr(CYBSP_LED_BLUE_PORT, CYBSP_LED_BLUE_NUM);
        /* Flash red LED for 300ms */
        Cy_GPIO_Set(CYBSP_LED_RED_PORT, CYBSP_LED_RED_NUM);

        if (red_led_timer != NULL)
        {
            xTimerReset(red_led_timer, 0);
        }
    }
    else if ( event == VA_EVENT_CMD_SILENCE_TIMEOUT )
    {
        if ((RUNNING_MODE != VA_MODE_WW_MULTI_CMD) && (RUNNING_MODE != VA_MODE_CMD_ONLY))
        {
            cur_voice_active = false;
            Cy_GPIO_Clr(CYBSP_LED_BLUE_PORT, CYBSP_LED_BLUE_NUM);
        }
    }
    else if ( event == VA_EVENT_CMD_DETECTED )
    {
        if(intent_text != NULL)
        {
            handle_command_for_ui = true;

            Cy_GPIO_Clr(CYBSP_LED_BLUE_PORT, CYBSP_LED_BLUE_NUM);
            /* Flash green LED for 300ms */
            Cy_GPIO_Set(CYBSP_LED_GREEN_PORT, CYBSP_LED_GREEN_NUM);

            if (green_led_timer != NULL)
            {
                xTimerReset(green_led_timer, 0);
            }
        }
    }
}

/*******************************************************************************
* Function Name: decrease_screen_brightness
********************************************************************************
* Summary:
*  Decreases the display brightness by a step value.
*
* Parameters:
*  void
*
* Return:
*  void
*
*******************************************************************************/
void decrease_screen_brightness()
{
    brightness_level  = lv_slider_get_value(ui_BrightnessSlider);
    
    if(brightness_level >= (lv_slider_get_min_value(ui_BrightnessSlider) + 20))
    {
        brightness_level -= 20;
    }
    else 
    {
        brightness_level = lv_slider_get_min_value(ui_BrightnessSlider);
    }

    if(brightness_level%20 !=0)
    {
        brightness_level = ((brightness_level) - (brightness_level%20));
    }

    if(brightness_level >= lv_slider_get_min_value(ui_BrightnessSlider) && brightness_level <= lv_slider_get_max_value(ui_BrightnessSlider))
    {
        lv_slider_set_value(ui_BrightnessSlider, brightness_level, LV_ANIM_OFF);
        mtb_display_st7701s_set_brightness(brightness_level);

        printf("Brightness level %d. \n", brightness_level);
    }
}

/*******************************************************************************
* Function Name: increase_screen_brightness
********************************************************************************
* Summary:
*  Increases the display brightness by a step value.
*
* Parameters:
*  void
*
* Return:
*  void
*
*******************************************************************************/
void increase_screen_brightness()
{
    brightness_level  = lv_slider_get_value(ui_BrightnessSlider);
  
    if((brightness_level + 20) <= lv_slider_get_max_value(ui_BrightnessSlider))
    {
        brightness_level += 20;
    }
    else 
    {
        brightness_level = lv_slider_get_max_value(ui_BrightnessSlider);
    }

    if(brightness_level%20 !=0)
    {
        brightness_level = ((brightness_level) - (brightness_level%20));
    }
    
    if(brightness_level >= lv_slider_get_min_value(ui_BrightnessSlider) && brightness_level <= lv_slider_get_max_value(ui_BrightnessSlider))
    {
        lv_slider_set_value(ui_BrightnessSlider, brightness_level, LV_ANIM_OFF);
        mtb_display_st7701s_set_brightness(brightness_level);

        printf("Brightness level %d. \n", brightness_level);
    }
}

/*******************************************************************************
* Function Name: go_to_settings_screen
********************************************************************************
* Summary:
*  Navigates the UI to the settings screen.
*
* Parameters:
*  void
*
* Return:
*  void
*
*******************************************************************************/
void go_to_settings_screen()
{
    lv_obj_t *current_screen = lv_scr_act();

    if(current_screen != ui_SettingsScreen)
    {
        lv_obj_add_flag(ui_voicecmdcontainer, LV_OBJ_FLAG_HIDDEN);

        _ui_screen_change(&ui_SettingsScreen, LV_SCR_LOAD_ANIM_FADE_ON, 150, 0, &ui_SettingsScreen_screen_init);
    }
}

/*******************************************************************************
* Function Name: go_to_active_screen
********************************************************************************
* Summary:
*  Navigates the UI to the active thermostat screen.
*
* Parameters:
*  void
*
* Return:
*  void
*
*******************************************************************************/
void go_to_active_screen()
{
    lv_obj_t *current_screen = lv_scr_act();

    if(current_screen != ui_ActiveScreen)
    {
        lv_obj_add_flag(ui_voicecmdcontainer, LV_OBJ_FLAG_HIDDEN);

        _ui_screen_change(&ui_ActiveScreen, LV_SCR_LOAD_ANIM_FADE_ON, 150, 0, &ui_ActiveScreen_screen_init);
    }
}

/*******************************************************************************
* Function Name: handle_VA_command
********************************************************************************
* Summary:
*  Processes the active voice assistant command and updates the thermostat
*  state and user interface as required.
*
* Parameters:
*  timer - LVGL timer context; unused when the command is invoked directly.
*
* Return:
*  void
*
*******************************************************************************/
void handle_VA_command(lv_timer_t * timer)
{
    va_cmd_present = false;
    /* Handle the command using a switch statement */
    bool is_temp_command = (va_cmd == VA_HMI_THERMOSTAT_DEMO_INTENT_SETTEMPERATURE_C      || 
                            va_cmd == VA_HMI_THERMOSTAT_DEMO_INTENT_MAKEITWARMER          ||
                            va_cmd == VA_HMI_THERMOSTAT_DEMO_INTENT_MAKEITCOOLER          );
    
    if (is_temp_command && !is_fahrenheit_resolved()) {
        return; 
    }

    switch (va_cmd) 
    {
        case VA_HMI_THERMOSTAT_DEMO_INTENT_SETTEMPERATURE_C:
        {
            if(intent_value)
            {
                update_device_temp((uint8_t)(intent_value));
                printf("Handling SETTEMPERATURE command.\n");
            }
            else
            {
                printf("Temperature not defined.\n");
            }
            break;
        }
        case VA_HMI_THERMOSTAT_DEMO_INTENT_INCREASETEMPERATURE:
        {
            if(intent_value)
            {
                update_device_temp((uint8_t)(get_current_temperature() + (uint8_t)(intent_value)));
            }
            else
            {
                update_device_temp((uint8_t)(get_current_temperature() + 1));
            }
            printf("Handling INCREASETEMPERATURE command.\n");
            break;
        }
        case VA_HMI_THERMOSTAT_DEMO_INTENT_DECREASETEMPERATURE:
        {
            if(intent_value)
            {
                update_device_temp((uint8_t)(get_current_temperature() - (uint8_t)(intent_value)));
            }
            else
            {
                update_device_temp((uint8_t)(get_current_temperature() - 1));
            }
            printf("Handling DECREASETEMPERATURE command.\n");
            break;
        }
        case VA_HMI_THERMOSTAT_DEMO_INTENT_MAKEITWARMER:
        {
            update_device_temp((uint8_t)(get_current_temperature() + 2));
            printf("Handling MAKEITWARMER command.\n");
            break;
        }
        case VA_HMI_THERMOSTAT_DEMO_INTENT_MAKEITCOOLER:
        {
            update_device_temp((uint8_t)(get_current_temperature() - 2));
            printf("Handling MAKEITCOOLER command.\n");
            break;
        }
        case VA_HMI_THERMOSTAT_DEMO_INTENT_INCREASESCREENBRIGHTNESS:
        {
            increase_screen_brightness();
            printf("Handling INCREASESCREENBRIGHTNESS command.\n");
            break;
        }
        case VA_HMI_THERMOSTAT_DEMO_INTENT_DECREASESCREENBRIGHTNESS:
        {
            decrease_screen_brightness();
            printf("Handling DECREASESCREENBRIGHTNESS command.\n");
            break;
        }
        case VA_HMI_THERMOSTAT_DEMO_INTENT_INCREASEFANSPEED:
        {
            fan_speed_t fan_mode = get_current_fan_mode();

            if(fan_mode != FAN_HIGH)
            {
                if (fan_mode == FAN_OFF)
                {
                    fan_mode = FAN_LOW; /* Start cycle at LOW */

                    update_thermostat_mode(MODE_ECO);
                }
                else if (fan_mode == FAN_LOW)
                {
                    fan_mode = FAN_MED;
                }
                else if (fan_mode == FAN_MED)
                {
                    fan_mode = FAN_HIGH;
                }

                current_settings.thermostat_setting.fan_mode = fan_mode;

                update_fan_mode(fan_mode);
                update_current_device_setting();
            }

            printf("Handling INCREASEFANSPEED command.\n");
            break;
        }
        case VA_HMI_THERMOSTAT_DEMO_INTENT_DECREASEFANSPEED:
        {
            fan_speed_t fan_mode = get_current_fan_mode();

            if((fan_mode != FAN_OFF) && (fan_mode != FAN_LOW))
            {
                if (fan_mode == FAN_HIGH)
                {
                    fan_mode = FAN_MED; /* Start cycle at LOW */
                }
                else if (fan_mode == FAN_MED)
                {
                    fan_mode = FAN_LOW;
                }

                current_settings.thermostat_setting.fan_mode = fan_mode;

                update_fan_mode(fan_mode);
                update_current_device_setting();
            }

            printf("Handling DECREASEFANSPEED command.\n");
            break;
        }
        case VA_HMI_THERMOSTAT_DEMO_INTENT_TURNONCMD:
        {
            thermostat_mode_t current_mode = get_current_device_mode();

            if(intent_value)
            {    
                if(current_mode == MODE_OFF)
                {
                    update_thermostat_mode(MODE_ECO);

                    current_settings.thermostat_setting.fan_mode = FAN_LOW;

                    update_fan_mode(FAN_LOW);
                    update_thermostat_mode_timer();
                    update_current_device_setting();
                }
            }

            printf("Handling TURNONCMD command.\n");
            break;
        }
        case VA_HMI_THERMOSTAT_DEMO_INTENT_TURNOFFCMD:
        {
            thermostat_mode_t current_mode = get_current_device_mode();

            if(intent_value)
            {
                if(current_mode != MODE_OFF)
                {
                    update_thermostat_mode(MODE_OFF);

                    current_settings.thermostat_setting.fan_mode = FAN_OFF;

                    update_fan_mode(FAN_OFF);
                    update_thermostat_mode_timer();
                    update_current_device_setting();
                }
            }

            printf("Handling TURNOFFCMD command.\n");
            break;
        }
        case VA_HMI_THERMOSTAT_DEMO_INTENT_SETTINGMODE:
        {
            go_to_settings_screen();
            printf("Handling SETTINGMODE command.\n");
            break;
        }
        case VA_HMI_THERMOSTAT_DEMO_INTENT_THERMOSTATMODE:
        {
            thermostat_mode_t current_mode = get_current_device_mode();

            if(intent_value)
            {
                const char* command = MTB_NLU_VARIABLE_PHRASE_LIST(PROJECT_PREFIX)[intent_value];
                
                if (strcmp(command, "eco") == 0)
                {    
                    current_mode = MODE_ECO;
                }
                else if (strcmp(command, "rapid") == 0)
                {
                    current_mode = MODE_RAPID;
                }
                else if (strcmp(command, "auto") == 0)
                {
                    current_mode = MODE_AUTO;
                }
            }
            else
            {
                if(current_mode == MODE_OFF)
                {
                    current_mode = MODE_ECO;
                }
                else if(current_mode == MODE_ECO)
                {
                    current_mode = MODE_RAPID;
                }
                else if(current_mode == MODE_RAPID)
                {
                    current_mode = MODE_AUTO;
                }
                else if(current_mode == MODE_AUTO)
                {
                    current_mode = MODE_ECO;
                }
            }

            update_thermostat_mode(current_mode);
            current_settings.thermostat_setting.mode = current_mode;
            current_settings.thermostat_setting.fan_mode = get_current_fan_mode();
            update_thermostat_mode_timer();
            update_current_device_setting();

            printf("Handling THERMOSTATMODE command.\n");
            break;
        }
        case VA_HMI_THERMOSTAT_DEMO_INTENT_WIFISTATUS:
        {
            wifi_status_handler();
            printf("Handling WIFISTATUS command.\n");
            break;
        }
        case VA_HMI_THERMOSTAT_DEMO_INTENT_MUTEVOLUME:
        {
            update_thermostat_volume(AUDIO_OFF);
            printf("Handling MUTEVOLUME command.\n");
            break;
        }
        case VA_HMI_THERMOSTAT_DEMO_INTENT_UNMUTEVOLUME:
        {
            update_thermostat_volume(AUDIO_MED);
            printf("Handling UNMUTEVOLUME command.\n");
            break;
        }

        default:
        {
            break;
        }
    }

}

/*******************************************************************************
* Function Name: handle_active_screen_loaded
********************************************************************************
* Summary:
*  Processes pending voice command after the active screen loads.
*
* Parameters:
*  e - Pointer to the LVGL event.
*
* Return:
*  void
*
*******************************************************************************/
void handle_active_screen_loaded(lv_event_t * e)
{
    if(va_cmd_present)
    {
        va_cmd_present = false;
        /* Create a timer to handle the commands after a delay */
        lv_timer_t * timer = lv_timer_create(handle_VA_command, 1300, NULL);
        lv_timer_set_repeat_count(timer, 1);
    }
}

/*******************************************************************************
* Function Name: handle_settings_screen_loaded
********************************************************************************
* Summary:
*  Processes pending voice command after the settings screen loads.
*
* Parameters:
*  e - Pointer to the LVGL event.
*
* Return:
*  void
*
*******************************************************************************/
void handle_settings_screen_loaded(lv_event_t * e)
{
    if(va_cmd_present)
    {
        va_cmd_present = false;
        /* Create a timer to handle the commands after a delay */
        lv_timer_t * timer = lv_timer_create(handle_VA_command, 300, NULL);
        lv_timer_set_repeat_count(timer, 1);
    }
}

/*******************************************************************************
* Function Name: intent_to_ui
********************************************************************************
* Summary:
*  Converts a voice assistant intent command to a UI action.
*
* Parameters:
*  command - The voice command string to process.
*
* Return:
*  va_rslt_t - Result of the command processing.
*
*******************************************************************************/
va_rslt_t intent_to_ui(const char *command)
{
    if(handle_command_for_ui)
    {
        handle_command_for_ui = false;

        if (NULL == command)
        {
            return VA_RSLT_INVALID_ARGUMENT; /* Return immediately for invalid input */
        }

        /* Map the command string to an enum value */
        va_cmd = va_command_to_id(command);

        if(va_cmd == VA_RSLT_FAIL)
        {
            /* Handle unknown commands */
            if (command && command[0] != '\0')
            {
                printf("Unknown command: %s\n", command);
            }
            else
            {
                printf("Unknown or empty command received.\n");
            }

            return VA_RSLT_INVALID_ARGUMENT; /* Return an error for unknown commands */
        }

        if((va_cmd == VA_HMI_THERMOSTAT_DEMO_INTENT_SETTINGMODE) ||
            (va_cmd == VA_HMI_THERMOSTAT_DEMO_INTENT_INCREASESCREENBRIGHTNESS) ||
            (va_cmd == VA_HMI_THERMOSTAT_DEMO_INTENT_DECREASESCREENBRIGHTNESS) ||
            (va_cmd == VA_HMI_THERMOSTAT_DEMO_INTENT_MUTEVOLUME) ||
            (va_cmd == VA_HMI_THERMOSTAT_DEMO_INTENT_UNMUTEVOLUME))
        {
            /* Execute command from Settings screen */
            if(lv_screen_active() == ui_SettingsScreen)
            {
                handle_VA_command(NULL);
                lv_obj_add_flag(ui_voicecmdcontainer, LV_OBJ_FLAG_HIDDEN);
                display_fan_anim();
            }
            else
            {
                va_cmd_present = true;
                /* Switch to Settings screen */
                go_to_settings_screen();
            }
        }
        else 
        {
            /* Execute command from Active screen */
            if(lv_screen_active() == ui_ActiveScreen)
            {
                /* Dismiss the connectivity popup if open so the command result is visible */
                if(popup_overlay_visible)
                {
                    switch_to_active_screen();
                }
                handle_VA_command(NULL);
                lv_obj_add_flag(ui_voicecmdcontainer, LV_OBJ_FLAG_HIDDEN);
                display_fan_anim();
            }
            else
            {
                va_cmd_present = true;
                /* Switch to Active screen */
                go_to_active_screen();
                display_fan_anim();
            }
        }
    }

    /* Successfully handled the command */
    return VA_RSLT_SUCCESS;
}

/*******************************************************************************
* Function Name: ww_to_ui
********************************************************************************
* Summary:
*  Handles wake-word detection and transitions the UI accordingly.
*
* Parameters:
*  void
*
* Return:
*  void
*
*******************************************************************************/
void ww_to_ui()
{
    if(handle_ww_for_ui)
    {
        handle_ww_for_ui = false;

        /* Check if the current screen is ui_LPScreen */
        if (lv_screen_active() == ui_LPScreen)
        {
            /** Switch to Active screen  */
            switch_to_active_screen();
            display_fan_anim();
       }


    }
}

/*******************************************************************************
 * Function Name: check_button_pressed
 *******************************************************************************
 * Summary:
 * Check if the user button is pressed.
 *
 * Parameters:
 *
 * Return:
 *  Returns true if the button is pressed, false otherwise
 *
 *******************************************************************************/
bool check_button_pressed(void)
{
    static uint32_t btn_count = 0;

    if (0 == Cy_GPIO_Read(CYBSP_USER_BTN1_PORT, CYBSP_USER_BTN1_NUM))
    {
        btn_count++;
    }
    else
    {
        btn_count = 0;
    }

    /* If the button is pressed for more than BUTTON_DEBOUNCE_COUNT, return true */
    if (btn_count > BUTTON_DEBOUNCE_COUNT)
    {
        btn_count = 0;
        return true;
    }
    return false;
}

/*******************************************************************************
 * Function Name: audio_enhancement_process_output
 *******************************************************************************
 * Summary:
 * AE output data for voice-assistant processing.
 *
 * Parameters:
 *  output_buffer: pointer to the output audio data buffer.
 *
 * Return:
 *  void
 *
 *******************************************************************************/
 
void audio_enhancement_process_output(ae_buffer_info_t *ae_output_buffer)
{
    va_event_t va_event;

#if AE_APP_PROFILE
    cy_afe_profile(AFE_PROFILE_CMD_PRINT_STATS_1SEC, NULL);
    cy_afe_profile(AFE_PROFILE_CMD_RESET, NULL);
#endif /* AE_APP_PROFILE */

    /* Use the output data from the audio enhancement with the voice-assistant */
    va_event = run_voice_assistant_process(ae_output_buffer->output_buf);

    if(bootup_done)
    {
        handle_va_process_output(va_event);
    }
}

/*******************************************************************************
 * Function Name: voice_assistant_task
 *******************************************************************************
 * Summary:
 * This is the FreeRTOS task to execute the DEEPCRAFT voice assistant routines.
 *
 * Parameters:
 *  void * arg
 *
 * Return:
 *  void
 *
 *******************************************************************************/
void voice_assistant_task(void * arg)
{
    va_rslt_t va_result;
    int16_t *audio_frame;
    int16_t *audio_feed_input;

#ifdef USE_AUDIO_ENHANCEMENT
    ae_rslt_t ae_result;
    uint8_t ae_license_error = 0;
#else
    va_event_t va_event;
#endif /* USE_AUDIO_ENHANCEMENT */    

    /* Initialize the profiler to print MCPS */
#if INFERENCING_PROFILE
    cy_profiler_init();
#endif /* INFERENCING_PROFILE */ 

   /* If AFE is used, initialize the audio enhancement */
#ifdef USE_AUDIO_ENHANCEMENT
#if AE_APP_PROFILE
    cy_profiler_init();
    cy_afe_profile(AFE_PROFILE_CMD_ENABLE,NULL);
#endif /* AE_APP_PROFILE */ 

    ae_result = audio_enhancement_init(AFE_INPUT_NUMBER_CHANNELS);
    if (ae_result != AE_RSLT_SUCCESS)
    {
        printf("Error initializing the audio enhancement. Error code=%d\r\n", ae_result);
        handle_app_error();
    }
    else
    {
        printf("Audio Enhancement initialized!\r\n");
    }
#endif /* USE_AUDIO_ENHANCEMENT */

    /* Initialize the voice assistant */
    va_result = voice_assistant_init(RUNNING_MODE);

    if (va_result != VA_RSLT_SUCCESS)
    {
        printf("Error initializing the voice assistant. Error code=%d\r\n", va_result);
        handle_app_error();
    }
    else
    {
        printf("Voice Assistant initialized!\r\n\r\n");
    }

    /* Initialize the PDM microphone */
    pdm_mic_init();

    /* Create one-shot FreeRTOS timers for LED flash indication */
    red_led_timer = xTimerCreate("RedLEDTimer", pdMS_TO_TICKS(300), pdFALSE, NULL, red_led_flash_timeout_cb);
    if (red_led_timer == NULL)
    {
        printf("Error: Failed to create red_led_timer\r\n");
    }

    green_led_timer = xTimerCreate("GreenLEDTimer", pdMS_TO_TICKS(300), pdFALSE, NULL, green_led_flash_timeout_cb);
    if (green_led_timer == NULL)
    {
        printf("Error: Failed to create green_led_timer\r\n");
    } 
    
    for (;;)
    {
        /* Get audio data */
        pdm_mic_get_data(&audio_frame);

        audio_feed_input = audio_frame;

#ifdef USE_AUDIO_ENHANCEMENT
        /* Apply the audio enhancement if AFE is enabled */
        ae_result = audio_enhancement_feed_input(audio_feed_input, NULL);
        if (ae_result == AE_RSLT_LICENSE_ERROR)
        {
            if(ae_license_error == 0)
            {
                ae_license_error = 1;
                   printf("ERROR! Audio Enhancement license expired!\r\n");
            }
        }
#else   
        /* If Audio Enhancement is not used, run voice-assistant 
         * directly with the microphone data */
        va_event = run_voice_assistant_process(audio_feed_input);

        if(bootup_done)
        {
            handle_va_process_output(va_event);
        }

#endif  /* USE_AUDIO_ENHANCEMENT */       

    }  
}
