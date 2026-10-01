/*******************************************************************************
 * File Name:   comm_manager.c
 *
 * Description: Implementation of IPC communication manager for sending
 *              thermostat data from CM33 core to CM55 core.
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
#include "FreeRTOS.h"
#include "timers.h"
#include "comm_manager.h"
#include "ipc_communication.h"
#include "wifi_task.h"
#include "wireless_manager.h"
#include "app_bt_gatt_handler.h"

CY_SECTION_SHAREDMEM static ipc_msg_cm33_to_cm55_t cm33_msg_data = 
{
    .client_id = CM55_IPC_PIPE_CLIENT_ID,
    .intr_mask = CY_IPC_CYPIPE_INTR_MASK_EP2,
    .data = 0,
    .data_len = 0
};

/* Sender-side confirmed-delivery cache for idempotent "STATE" commands
 * (last-value-wins UI state such as fan speed, conn state, sync strings).
 * EVENT/ACTION commands (GET_*, DISABLE_TOUCH, *_SYNC time fields, etc.) are
 * NOT in this table and are always sent unconditionally - a repeat of those
 * is semantically meaningful and must never be swallowed.
 *
 * Rule: only commit (has_confirmed=true) once cm33_send_msg_cm55() reports
 * success. A failed send is marked `pending` and its exact payload is kept so
 * the retry timer below can resend it - never wait for a periodic full resync,
 * and never risk a permanently stale UI from silently dropping the one message
 * that would have moved the state forward. */
typedef enum
{
    CM33_PAYLOAD_TYPE_INT,
    CM33_PAYLOAD_TYPE_STRING,
    CM33_PAYLOAD_TYPE_UID,
} cm33_payload_type_t;

typedef struct
{
    uint8_t             cmd;
    cm33_payload_type_t payload_type;
    int                 int_val;
    uint8_t             data_len;
    char                buf[32];
    bool                has_confirmed;
    bool                pending;
} cm33_ipc_cache_entry_t;

static cm33_ipc_cache_entry_t cm33_ipc_cache_table[] =
{
    { IPC_CMD_UPDATE_CONN_STATE,      CM33_PAYLOAD_TYPE_INT,    0, 0, {0}, false, false },
    { IPC_CMD_SET_DISPLAY_BRIGHTNESS, CM33_PAYLOAD_TYPE_INT,    0, 0, {0}, false, false },
    { IPC_CMD_SET_AUDIO_LEVEL,        CM33_PAYLOAD_TYPE_INT,    0, 0, {0}, false, false },
    { IPC_CMD_SET_TEMP_UNIT,          CM33_PAYLOAD_TYPE_INT,    0, 0, {0}, false, false },
    { IPC_CMD_SET_FAN_SPEED,          CM33_PAYLOAD_TYPE_INT,    0, 0, {0}, false, false },
    { IPC_CMD_SET_THERMOSTAT_MODE,    CM33_PAYLOAD_TYPE_INT,    0, 0, {0}, false, false },
    { IPC_CMD_SET_TARGET_TEMP,        CM33_PAYLOAD_TYPE_INT,    0, 0, {0}, false, false },
    { IPC_CMD_UPDATE_PROVISION_STATE, CM33_PAYLOAD_TYPE_INT,    0, 0, {0}, false, false },
    { IPC_CMD_UPDATE_PRESENCE_STATUS, CM33_PAYLOAD_TYPE_INT,    0, 0, {0}, false, false },
    { IPC_CMD_UPDATE_CURRENT_SCREEN,  CM33_PAYLOAD_TYPE_INT,    0, 0, {0}, false, false },
    { IPC_CMD_SET_UID,                CM33_PAYLOAD_TYPE_UID,    0, 0, {0}, false, false },
    { IPC_CMD_LOCATION_SYNC,          CM33_PAYLOAD_TYPE_STRING, 0, 0, {0}, false, false },
    { IPC_CMD_WEATHER_SYNC,           CM33_PAYLOAD_TYPE_STRING, 0, 0, {0}, false, false },
    { IPC_CMD_WEATHER_CODE_SYNC,      CM33_PAYLOAD_TYPE_STRING, 0, 0, {0}, false, false },
};

#define CM33_IPC_CACHE_TABLE_LEN (sizeof(cm33_ipc_cache_table) / sizeof(cm33_ipc_cache_table[0]))

static TimerHandle_t cm33_ipc_cache_retry_timer = NULL;
static SemaphoreHandle_t cm33_ipc_cache_mutex = NULL;

/*******************************************************************************
* Function Name: cm33_ipc_find_cache_entry
********************************************************************************
* Summary:
*  Looks up the cache table entry for the given IPC command, if any.
*
* Parameters:
*  cmd - IPC command ID to look up.
*
* Return:
*  cm33_ipc_cache_entry_t* - Pointer to the entry, or NULL if cmd has no
*  cache table entry (an EVENT/ACTION command).
*
*******************************************************************************/
static cm33_ipc_cache_entry_t *cm33_ipc_find_cache_entry(uint8_t cmd)
{
    for (size_t i = 0; i < CM33_IPC_CACHE_TABLE_LEN; i++)
    {
        if (cm33_ipc_cache_table[i].cmd == cmd)
        {
            return &cm33_ipc_cache_table[i];
        }
    }
    return NULL;
}

/*******************************************************************************
* Function Name: cm33_ipc_send_cache_entry
********************************************************************************
* Summary:
*  Sends the entry's currently-cached payload and updates confirmed/pending
*  from the result. Used both for a fresh value change and for retrying an
*  unconfirmed one - the cache always holds the payload that still needs to
*  reach CM55.
*
* Parameters:
*  entry - Cache table entry to send.
*
* Return:
*  void
*
*******************************************************************************/
static void cm33_ipc_send_cache_entry(cm33_ipc_cache_entry_t *entry)
{
    int status;

    if (!cm33_ipc_lock())
    {
        /* Buffer still in use by CM55; keep the payload pending so the retry
         * timer resends it rather than dropping the state update. */
        entry->pending = true;
        return;
    }
    cm33_msg_data.cmd = entry->cmd;

    switch (entry->payload_type)
    {
        case CM33_PAYLOAD_TYPE_UID:
            memcpy(cm33_msg_data.unique_id, entry->buf, sizeof(cm33_msg_data.unique_id));
            status = cm33_send_msg_cm55(&cm33_msg_data);
            break;

        case CM33_PAYLOAD_TYPE_STRING:
            memset(cm33_msg_data.char_value, 0, sizeof(cm33_msg_data.char_value));
            strncpy(cm33_msg_data.char_value, entry->buf, sizeof(cm33_msg_data.char_value) - 1);
            status = cm33_send_msg_cm55(&cm33_msg_data);
            break;

        case CM33_PAYLOAD_TYPE_INT:
        default:
            cm33_msg_data.data = entry->int_val;
            cm33_msg_data.data_len = entry->data_len;
            status = cm33_send_msg_cm55(&cm33_msg_data);
            break;
    }
    cm33_ipc_unlock();

    entry->pending = (status != 0);
    if (status == 0)
    {
        entry->has_confirmed = true;
    }
}

/*******************************************************************************
* Function Name: cm33_ipc_cache_retry_timer_cb
********************************************************************************
* Summary:
*  Low-frequency sweep: resends any STATE command whose last delivery was not
*  confirmed (pipe-level send failure only - a full CM55 rx queue is not
*  reported back to CM33, so that drop path is not covered by this retry).
*  Zero traffic while every cache entry is confirmed and unchanged.
*
* Parameters:
*  xTimer - Timer handle (unused).
*
* Return:
*  void
*
*******************************************************************************/
static void cm33_ipc_cache_retry_timer_cb(TimerHandle_t xTimer)
{
    CY_UNUSED_PARAMETER(xTimer);

    xSemaphoreTake(cm33_ipc_cache_mutex, portMAX_DELAY);
    for (size_t i = 0; i < CM33_IPC_CACHE_TABLE_LEN; i++)
    {
        if (cm33_ipc_cache_table[i].pending)
        {
            cm33_ipc_send_cache_entry(&cm33_ipc_cache_table[i]);
        }
    }
    xSemaphoreGive(cm33_ipc_cache_mutex);
}

/*******************************************************************************
* Function Name: comm_manager_cache_init
********************************************************************************
* Summary:
*  Starts the low-frequency retry timer that guarantees eventual delivery of
*  STATE-class IPC commands whose send was not confirmed (retry until
*  confirmed instead of a periodic full resync). Call once during CM33 startup,
*  before or after the scheduler starts.
*
* Parameters:
*  void
*
* Return:
*  void
*
*******************************************************************************/
void comm_manager_cache_init(void)
{
    cm33_ipc_cache_mutex = xSemaphoreCreateMutex();
    if (cm33_ipc_cache_mutex == NULL)
    {
        printf("comm_manager_cache_init: failed to create cache mutex\n");
        return;
    }

    cm33_ipc_cache_retry_timer = xTimerCreate("IpcCacheRetry", pdMS_TO_TICKS(2000),
                                               pdTRUE, NULL, cm33_ipc_cache_retry_timer_cb);
    if (cm33_ipc_cache_retry_timer != NULL)
    {
        xTimerStart(cm33_ipc_cache_retry_timer, 0);
    }
}

/* Shared send helpers: hold the lock across populate+send so callers don't
 * each have to repeat the lock/unlock boilerplate around cm33_msg_data. */

/*******************************************************************************
* Function Name: cm33_ipc_send_int
********************************************************************************
* Summary:
*  Populates and sends an int-payload IPC command. If cmd has a cache table
*  entry and data_len != 0 (a STATE value, not a signal), an unchanged value
*  that was already confirmed-delivered is skipped entirely; otherwise
*  (EVENT/ACTION commands, or any cache-table miss) the send always happens
*  unconditionally. data_len == 0 means "no data" (a trigger/signal, not a
*  state value) - always sent unconditionally even if a table entry exists,
*  since there is no value to compare against and a repeat trigger is
*  meaningful, not a duplicate.
*
* Parameters:
*  cmd      - IPC command ID.
*  data     - Int payload value.
*  data_len - Payload length in bytes (0 means no value to compare/cache).
*
* Return:
*  void
*
*******************************************************************************/
static void cm33_ipc_send_int(uint8_t cmd, int data, uint8_t data_len)
{
    cm33_ipc_cache_entry_t *entry = cm33_ipc_find_cache_entry(cmd);

    if ((entry != NULL) && (data_len != 0))
    {
        xSemaphoreTake(cm33_ipc_cache_mutex, portMAX_DELAY);
        if (entry->has_confirmed && !entry->pending && entry->int_val == data)
        {
            xSemaphoreGive(cm33_ipc_cache_mutex);
            return;
        }
        entry->int_val = data;
        entry->data_len = data_len;
        cm33_ipc_send_cache_entry(entry);
        xSemaphoreGive(cm33_ipc_cache_mutex);
        return;
    }

    if (!cm33_ipc_lock())
    {
        printf("cm33_ipc_send_int: buffer busy, dropping cmd %u\n", cmd);
        return;
    }
    cm33_msg_data.cmd = cmd;
    cm33_msg_data.data = data;
    cm33_msg_data.data_len = data_len;
    cm33_send_msg_cm55(&cm33_msg_data);
    cm33_ipc_unlock();
}

/*******************************************************************************
* Function Name: cm33_ipc_send_signal
********************************************************************************
* Summary:
*  Sends a no-payload IPC command (a trigger/signal). Always sent
*  unconditionally - not eligible for caching.
*
* Parameters:
*  cmd - IPC command ID.
*
* Return:
*  void
*
*******************************************************************************/
static void cm33_ipc_send_signal(uint8_t cmd)
{
    if (!cm33_ipc_lock())
    {
        printf("cm33_ipc_send_signal: buffer busy, dropping cmd %u\n", cmd);
        return;
    }
    cm33_msg_data.cmd = cmd;
    cm33_send_msg_cm55(&cm33_msg_data);
    cm33_ipc_unlock();
}

/*******************************************************************************
* Function Name: cm33_ipc_send_string
********************************************************************************
* Summary:
*  Populates and sends a string-payload IPC command. If cmd has a cache
*  table entry, an unchanged value that was already confirmed-delivered is
*  skipped entirely; otherwise the send always happens unconditionally.
*
* Parameters:
*  cmd   - IPC command ID.
*  value - Null-terminated string payload.
*
* Return:
*  void
*
*******************************************************************************/
static void cm33_ipc_send_string(uint8_t cmd, const char *value)
{
    cm33_ipc_cache_entry_t *entry = cm33_ipc_find_cache_entry(cmd);

    if (entry != NULL)
    {
        xSemaphoreTake(cm33_ipc_cache_mutex, portMAX_DELAY);
        if (entry->has_confirmed && !entry->pending &&
            strncmp(entry->buf, value, sizeof(entry->buf) - 1) == 0)
        {
            xSemaphoreGive(cm33_ipc_cache_mutex);
            return;
        }
        strncpy(entry->buf, value, sizeof(entry->buf) - 1);
        entry->buf[sizeof(entry->buf) - 1] = '\0';
        cm33_ipc_send_cache_entry(entry);
        xSemaphoreGive(cm33_ipc_cache_mutex);
        return;
    }

    if (!cm33_ipc_lock())
    {
        printf("cm33_ipc_send_string: buffer busy, dropping cmd %u\n", cmd);
        return;
    }
    cm33_msg_data.cmd = cmd;
    memset(cm33_msg_data.char_value, 0, sizeof(cm33_msg_data.char_value));
    strncpy(cm33_msg_data.char_value, value, sizeof(cm33_msg_data.char_value) - 1);
    cm33_send_msg_cm55(&cm33_msg_data);
    cm33_ipc_unlock();
}

/*******************************************************************************
* Function Name: cm33_ipc_send_uid
********************************************************************************
* Summary:
*  Populates and sends the fixed-size (13 byte) UID payload. If cmd has a
*  cache table entry, an unchanged value that was already confirmed-delivered
*  is skipped entirely; otherwise the send always happens unconditionally.
*
* Parameters:
*  cmd - IPC command ID.
*  uid - 13-byte UID payload (not necessarily null-terminated).
*
* Return:
*  void
*
*******************************************************************************/
static void cm33_ipc_send_uid(uint8_t cmd, const char *uid)
{
    cm33_ipc_cache_entry_t *entry = cm33_ipc_find_cache_entry(cmd);

    if (entry != NULL)
    {
        xSemaphoreTake(cm33_ipc_cache_mutex, portMAX_DELAY);
        if (entry->has_confirmed && !entry->pending && memcmp(entry->buf, uid, 13) == 0)
        {
            xSemaphoreGive(cm33_ipc_cache_mutex);
            return;
        }
        memcpy(entry->buf, uid, 13);
        cm33_ipc_send_cache_entry(entry);
        xSemaphoreGive(cm33_ipc_cache_mutex);
        return;
    }

    if (!cm33_ipc_lock())
    {
        printf("cm33_ipc_send_uid: buffer busy, dropping cmd %u\n", cmd);
        return;
    }
    cm33_msg_data.cmd = cmd;
    memcpy(cm33_msg_data.unique_id, uid, 13);
    cm33_send_msg_cm55(&cm33_msg_data);
    cm33_ipc_unlock();
}

/*******************************************************************************
* Function Name: set_fan_speed
********************************************************************************
* Summary:
*  Sends IPC command to set fan speed.
*
* Parameters:
*  value - Fan speed value.
*
* Return:
*  void
*
*******************************************************************************/
void set_fan_speed(fan_speed_t value)
{
    cm33_ipc_send_int(IPC_CMD_SET_FAN_SPEED, value, 1);
}


/*******************************************************************************
* Function Name: set_device_mode
********************************************************************************
* Summary:
*  Sends IPC command to set thermostat mode.
*
* Parameters:
*  value - Thermostat mode value.
*
* Return:
*  void
*
*******************************************************************************/
void set_device_mode(thermostat_mode_t value)
{
    cm33_ipc_send_int(IPC_CMD_SET_THERMOSTAT_MODE, value, 1);
}

/*******************************************************************************
* Function Name: set_device_temp
********************************************************************************
* Summary:
*  Sends IPC command to set target temperature.
*
* Parameters:
*  value - Target temperature value.
*
* Return:
*  void
*
*******************************************************************************/
void set_device_temp(uint8_t value)
{
    cm33_ipc_send_int(IPC_CMD_SET_TARGET_TEMP, value, 1);
}

/*******************************************************************************
* Function Name: set_device_brightness
********************************************************************************
* Summary:
*  Sends IPC command to set display brightness.
*
* Parameters:
*  level - Brightness level.
*
* Return:
*  void
*
*******************************************************************************/
void set_device_brightness(uint8_t level)
{
    cm33_ipc_send_int(IPC_CMD_SET_DISPLAY_BRIGHTNESS, level, 1);
}

/*******************************************************************************
* Function Name: set_device_audio_level
********************************************************************************
* Summary:
*  Sends IPC command to set audio level.
*
* Parameters:
*  level - Audio level.
*
* Return:
*  void
*
*******************************************************************************/
void set_device_audio_level(uint8_t level)
{
    cm33_ipc_send_int(IPC_CMD_SET_AUDIO_LEVEL, level, 1);
}

/*******************************************************************************
* Function Name: display_ble_pin
********************************************************************************
* Summary:
*  Sends IPC command to display BLE PIN.
*
* Parameters:
*  pin - BLE PIN value.
*
* Return:
*  void
*
*******************************************************************************/
void display_ble_pin(uint32_t pin)
{
    /* Showing the passkey moves the CM55 UI off the "connected" state, so drop
     * the cached conn-state; otherwise the post-pairing BLE_CONNECTED (same
     * value already sent at GATT link-up) is deduped and the pairing window sticks. */
    if (pin > 1)
    {
        cm33_ipc_cache_entry_t *entry = cm33_ipc_find_cache_entry(IPC_CMD_UPDATE_CONN_STATE);
        if (entry != NULL)
        {
            xSemaphoreTake(cm33_ipc_cache_mutex, portMAX_DELAY);
            entry->has_confirmed = false;
            xSemaphoreGive(cm33_ipc_cache_mutex);
        }
    }
    cm33_ipc_send_int(IPC_CMD_CURRENT_EVENT, pin, 1);
}

/*******************************************************************************
* Function Name: hide_connectivity_popup_ipc
********************************************************************************
* Summary:
*  Tells CM55 to dismiss the connectivity/pairing popups (called when the
*  mobile app sends data over BLE).
*
* Parameters:
*  void
*
* Return:
*  void
*
*******************************************************************************/
void hide_connectivity_popup_ipc(void)
{
    cm33_ipc_send_int(IPC_CMD_HIDE_CONN_POPUP, 0, 0);
}

/*******************************************************************************
* Function Name: update_conn_state
********************************************************************************
* Summary:
*  Updates connection state via IPC.
*
* Parameters:
*  state - Connection state value.
*
* Return:
*  void
*
*******************************************************************************/
void update_conn_state(uint32_t state)
{
    /*If a Wi-Fi cancel operation is in progress, do not update the Wi-Fi-related UI,
     *except for a fresh Wi-Fi connect attempt, which must still show its own progress*/
    if(is_wifi_operation_canceled())
    {
        if((DEV_ST_WIFI_STATE_MIN <= state) && (state <= DEV_ST_WIFI_STATE_MAX)
            && (DEV_ST_WIFI_CONNECTING != state))
        {
            return;
        }
    }

    /*If a BLE is connected, do not update the Wi-Fi-related UI, except for a fresh
     *Wi-Fi connect attempt, which must still show its own progress/cancel screen*/
    if(get_ble_connection_status() && (DEV_ST_WIFI_CONNECTING != state))
    {
        state = DEV_ST_BLE_CONNECTED;
    }
    cm33_ipc_send_int(IPC_CMD_UPDATE_CONN_STATE, state, 1);
}


/*******************************************************************************
* Function Name: update_current_screen
********************************************************************************
* Summary:
*  Sends IPC command to update current screen.
*
* Parameters:
*  screen - Screen ID to display.
*
* Return:
*  void
*
*******************************************************************************/
void update_current_screen(screen_id_t screen)
{
    cm33_ipc_send_int(IPC_CMD_UPDATE_CURRENT_SCREEN, screen, 1);
}

/*******************************************************************************
* Function Name: response_uid_req
********************************************************************************
* Summary:
*  Sends UID response via IPC.
*
* Parameters:
*  uid - Pointer to UID string.
*
* Return:
*  void
*
*******************************************************************************/
void response_uid_req(char *uid)
{
    cm33_ipc_send_uid(IPC_CMD_SET_UID, uid);
}

/*******************************************************************************
* Function Name: set_device_audio
********************************************************************************
* Summary:
*  Sends IPC command to set audio level.
*
* Parameters:
*  value - Audio level value.
*
* Return:
*  void
*
*******************************************************************************/
void set_device_audio(audio_level_t value)
{
    cm33_ipc_send_int(IPC_CMD_SET_AUDIO_LEVEL, value, 1);
}

/*******************************************************************************
* Function Name: set_device_temp_unit
********************************************************************************
* Summary:
*  Sends IPC command to set temperature unit.
*
* Parameters:
*  value - Temperature unit value.
*
* Return:
*  void
*
*******************************************************************************/
void set_device_temp_unit(temp_unit_t value)
{
    cm33_ipc_send_int(IPC_CMD_SET_TEMP_UNIT, value, 1);
}

/*******************************************************************************
* Function Name: update_presence_detection_status
********************************************************************************
* Summary:
*  Sends IPC command to update presence detection status.
*
* Parameters:
*  status - Presence detection status.
*
* Return:
*  void
*
*******************************************************************************/
void update_presence_detection_status(presence_status_t status)
{
    cm33_ipc_send_int(IPC_CMD_UPDATE_PRESENCE_STATUS, status, 1);
}

/*******************************************************************************
* Function Name: update_provision_state
********************************************************************************
* Summary:
*  Sends IPC command to update provision state.
*
* Parameters:
*  state - Provision state.
*
* Return:
*  void
*
*******************************************************************************/
void update_provision_state(bool state)
{
    cm33_ipc_send_int(IPC_CMD_UPDATE_PROVISION_STATE, state, 1);
}

/*******************************************************************************
* Function Name: set_datetime_ui
********************************************************************************
* Summary:
*  Sends date/time data via IPC.
*
* Parameters:
*  timestamp - Pointer to DateTime structure.
*
* Return:
*  void
*
*******************************************************************************/
void set_datetime_ui(DateTime *timestamp)
{
    if (!cm33_ipc_lock())
    {
        printf("set_datetime_ui: buffer busy, dropping date/time send\n");
        return;
    }
    cm33_msg_data.cmd = IPC_CMD_SET_DATE_TIME;
    memcpy(&cm33_msg_data.datetime, (DateTime*)timestamp, sizeof(DateTime));

    cm33_send_msg_cm55(&cm33_msg_data);
    cm33_ipc_unlock();
}

/*******************************************************************************
* Function Name: no_internet_connected_send_to_ui
********************************************************************************
* Summary:
*  Sends no internet connection notification via IPC.
*
* Parameters:
*  void
*
* Return:
*  void
*
*******************************************************************************/
void no_internet_connected_send_to_ui(void)
{
    cm33_ipc_send_signal(IPC_CMD_NO_INTERNET_NOTIFY);
}


/*******************************************************************************
* Function Name: disable_touch
********************************************************************************
* Summary:
*  Sends IPC command to disable touch input.
*
* Parameters:
*  void
*
* Return:
*  void
*
*******************************************************************************/
void disable_touch(void)
{
    cm33_ipc_send_signal(IPC_CMD_DISABLE_TOUCH);
}

/*******************************************************************************
* Function Name: set_weather_sync
********************************************************************************
* Summary:
*  Sends weather data synchronization via IPC.
*
* Parameters:
*  value - Weather data string.
*
* Return:
*  void
*
*******************************************************************************/
void set_weather_sync(const char *value)
{
    cm33_ipc_send_string(IPC_CMD_WEATHER_SYNC, value);
}

/*******************************************************************************
* Function Name: set_location_sync
********************************************************************************
* Summary:
*  Sends location data synchronization via IPC.
*
* Parameters:
*  value - Location data string.
*
* Return:
*  void
*
*******************************************************************************/
void set_location_sync(const char *value)
{
    cm33_ipc_send_string(IPC_CMD_LOCATION_SYNC, value);
}

/*******************************************************************************
* Function Name: set_weather_code_sync
********************************************************************************
* Summary:
*  Sends weather code synchronization via IPC.
*
* Parameters:
*  value - Weather code data string.
*
* Return:
*  void
*
*******************************************************************************/
void set_weather_code_sync(const char *value)
{
    cm33_ipc_send_string(IPC_CMD_WEATHER_CODE_SYNC, value);
}

/*******************************************************************************
* Function Name: signal_http_fetching_start
********************************************************************************
* Summary:
*  Sends IPC signal that HTTP fetching has started.
*
* Parameters:
*  void
*
* Return:
*  void
*
*******************************************************************************/
void signal_http_fetching_start(void)
{
    cm33_ipc_send_int(IPC_CMD_HTTP_FETCHING_START, 0, 0);
}

/*******************************************************************************
* Function Name: signal_http_fetch_failed
********************************************************************************
* Summary:
*  Sends IPC signal that the HTTP data fetch failed.
*
* Parameters:
*  void
*
* Return:
*  void
*
*******************************************************************************/
void signal_http_fetch_failed(void)
{
    cm33_ipc_send_int(IPC_CMD_HTTP_FETCH_FAILED, 0, 0);
}

