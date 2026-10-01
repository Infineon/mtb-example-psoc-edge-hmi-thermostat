/*******************************************************************************
 * File Name:  thermostat_connectivity.c
 *
 * Description: Thermostat connectivity, provisioning UI and power/idle timers.
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
 *                             VARIABLES
 ******************************************************************************/

/* Flag to prevent overlay dismiss during the auto-hide timer window */
static bool popup_auto_hide_pending = false;

static lv_timer_t *app_timer = NULL;
static lv_timer_t *lvgl_timer = NULL;

connection_retries_t wifi_reconnect_retry     = WIFI_CONN_RETRY_LIMITED;
connection_retries_t cloud_reconnect_retry     = CLOUD_CONN_RETRY_LIMITED;
connection_retries_t ble_reconnect_retry     = BLE_CONN_RETRY_LIMITED;

static provisioning_method_t prov_method = PROV_MAPP_BLE;
static qr_manager_t qr_manager;

/* Device current and previous connection state */
device_connection_state_t dev_current_conn_state = DEV_ST_UNPROVISIONED;
device_connection_state_t dev_last_conn_state = DEV_ST_UNPROVISIONED;

static volatile bool ble_conn_state = false;
static volatile bool wifi_conn_state = false;
static volatile bool cloud_conn_state = false;

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
        ble_pairing_popup_visible = false;

        /* Start fan mode animation running in background */
        display_fan_anim();
        display_temp_change_anim();

        _ui_flag_modify(ui_ArcGroup, LV_OBJ_FLAG_HIDDEN, _UI_MODIFY_FLAG_REMOVE);
    }
}

/*******************************************************************************
* Function Name: dismiss_connectivity_popups
********************************************************************************
* Summary:
*  Unconditionally closes the connectivity overlay (which also contains the BLE
*  pairing PIN window) and clears the popup state. Called when the mobile app
*  sends data over BLE.
*
* Parameters:
*  void
*
* Return:
*  void
*
*******************************************************************************/
void dismiss_connectivity_popups(void)
{
    if (lv_obj_has_flag(ui_popupoverlay, LV_OBJ_FLAG_HIDDEN))
    {
        return;
    }
    ui_timer_stop();
    lv_obj_add_flag(ui_popupoverlay, LV_OBJ_FLAG_HIDDEN);
    ble_pairing_popup_visible = false;
    wifi_popup_state = false;
    popup_overlay_visible = false;
    _ui_flag_modify(ui_ArcGroup, LV_OBJ_FLAG_HIDDEN, _UI_MODIFY_FLAG_REMOVE);
    display_fan_anim();
    display_temp_change_anim();
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

    /* Repeat count reaching 0 must pause the timer, not free it, since ui_timer_start()
     * re-arms this same instance for the rest of the app's lifetime. */
    lv_timer_set_auto_delete(lvgl_timer, false);
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

    /* While the BLE pairing code popup is visible, do not render other
     * connectivity states on top of it. Only terminal states that resolve
     * the pairing (BLE connected / unprovisioned) or the pairing state
     * itself are allowed through. */
    if (ble_pairing_popup_visible &&
        (state != DEV_ST_BLE_CONNECTED) &&
        (state != DEV_ST_UNPROVISIONED) &&
        (state != DEV_ST_BLE_PAIRING))
    {

        switch (state)
        {
            case DEV_ST_CLOUD_CONNECTED:
                is_device_connected = true;
                ble_conn_state = false;
                cloud_conn_state = true;
                break;
            case DEV_ST_CLOUD_CONNECTING:
                cloud_conn_state = false;
                break;
            case DEV_ST_WIFI_CONNECTING:
                wifi_conn_state = false;
                cloud_conn_state = false;
                break;
            case DEV_ST_CLOUD_DISCONNECTED:
                is_device_connected = false;
                cloud_conn_state = false;
                break;
            case DEV_ST_WIFI_CONNECTED:
                wifi_conn_state = true;
                break;
            case DEV_ST_WIFI_DISCONNECTED:
                wifi_conn_state = false;
                break;
            case DEV_ST_BLE_ADVERTISING:
                ble_conn_state = false;
                break;
            default:
                break;
        }

        dev_last_conn_state = dev_current_conn_state;
        dev_current_conn_state = state;
        return;
    }

    /* A terminal state resolves the pairing, clear the popup flag */
    if ((state == DEV_ST_BLE_CONNECTED) || (state == DEV_ST_UNPROVISIONED))
    {
        ble_pairing_popup_visible = false;
    }

    /* Hide all hidden widgets (images, button, labels) initially */
    lv_obj_add_flag(ui_commissionMapp, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ui_commissionKeyboard, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ui_autoreconnectpanel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ui_connstatekeyboardbtn, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ui_devconnstatecontianer, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ui_qrcodecontainer, LV_OBJ_FLAG_HIDDEN);
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
    lv_obj_add_flag(ui_blepairingcode, LV_OBJ_FLAG_HIDDEN);

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
            lv_obj_clear_flag(ui_devconnstatecontianer, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(ui_cloudconnected, LV_OBJ_FLAG_HIDDEN);
            state_text = "Cloud Connected";
            lv_obj_clear_flag(ui_qrcodebtn, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(ui_switchtoKeybd, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(ui_wifideletebtn, LV_OBJ_FLAG_HIDDEN);

            /* Update icon on home screen */
            lv_obj_clear_flag(ui_homecloudconnimg, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(ui_bleswitchbtn, LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_style_opa(ui_homecloudconnimg, 255, 0);
            lv_obj_add_flag(ui_blepairingcode, LV_OBJ_FLAG_HIDDEN);

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
            lv_obj_add_flag(ui_blepairingcode, LV_OBJ_FLAG_HIDDEN);
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
            lv_obj_add_flag(ui_blepairingcode, LV_OBJ_FLAG_HIDDEN);
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
            lv_obj_add_flag(ui_blepairingcode, LV_OBJ_FLAG_HIDDEN);
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
            lv_obj_add_flag(ui_blepairingcode, LV_OBJ_FLAG_HIDDEN);
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
            lv_obj_clear_flag(ui_blepairingcode, LV_OBJ_FLAG_HIDDEN);
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
            lv_obj_add_flag(ui_blepairingcode, LV_OBJ_FLAG_HIDDEN);
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
            lv_obj_add_flag(ui_blepairingcode, LV_OBJ_FLAG_HIDDEN);
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
            lv_obj_add_flag(ui_blepairingcode, LV_OBJ_FLAG_HIDDEN);
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
            lv_obj_add_flag(ui_blepairingcode, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(ui_bleconnected120, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(ui_mappinfobutton, LV_OBJ_FLAG_HIDDEN);

            /* Only show the switch to WiFi option if device is provisioned  */
            if ( true == is_device_provisioned || wifi_conn_state)
            {
                lv_obj_clear_flag(ui_switchtowifilbl, LV_OBJ_FLAG_HIDDEN);
                lv_obj_add_flag(ui_connstatekeyboardbtn, LV_OBJ_FLAG_HIDDEN);
                lv_obj_add_flag(ui_commissionMapp, LV_OBJ_FLAG_HIDDEN);
            }
            else
            {
                lv_obj_add_flag(ui_mappinfolabel, LV_OBJ_FLAG_HIDDEN);
            }

            state_text = "BLE Connected";

            /* Update icon on home screen */
            lv_obj_clear_flag(ui_bleconnected50, LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_style_opa(ui_bleconnected50, 190, 0);

            /* Start timer to hide the pop-up screen */
            ui_timer_stop();
            ui_timer_start();
            ble_conn_state = true;
            break;

        default:
            break;
    }

    /* Update device state label in popup screen */
    lv_label_set_text(ui_devstatelabel, state_text);
    
    /* Update device current connection state */
    dev_last_conn_state = dev_current_conn_state;
    dev_current_conn_state = state;
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
        ble_pairing_popup_visible = false;
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
    lv_obj_add_flag(ui_blepairingcode, LV_OBJ_FLAG_HIDDEN);
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
        if (app_timer == NULL)
        {
            LOG_ERROR(CYLF_DEF, "start_active_state_timer: failed to create app_timer\n");
            return;
        }
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

void display_ble_pairing_window(bool hide, char *code)
{
    if(hide)
    {
        /* Pairing window is being dismissed, allow other connectivity
         * states to render again. */
        ble_pairing_popup_visible = false;

        /* Check if Wi-Fi pop up is visible on screen.
         * If visible, don't hide connection screen. */
        if(wifi_popup_state != true)
        {
            lv_obj_add_flag(ui_popupoverlay,LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(ui_TemperatureArcBgPanel,LV_OBJ_FLAG_HIDDEN);
        }
    }
    else
    {
        /* Pairing code popup is now visible, block other connectivity
         * states from displaying on top of it. */
        ble_pairing_popup_visible = true;

        lv_obj_add_flag(ui_commissionMapp,LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ui_commissionKeyboard,LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ui_devconnstatecontianer,LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ui_qrcodecontainer,LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ui_popupoverlay, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ui_devconnstatecontianer, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(ui_popupoverlay, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(ui_blepairingcode, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text_fmt(ui_pinlabel, "%s", code);
    }
}


/* [] END OF FILE */
