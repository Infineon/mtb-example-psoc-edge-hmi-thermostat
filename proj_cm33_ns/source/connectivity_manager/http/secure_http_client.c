/*******************************************************************************
* File Name: secure_http_client.c
*
* Description: This file contains the necessary functions to start the HTTPS
* client and send GET, POST, and PUT request to the HTTPS client.
*
* Related Document: See README.md
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
#include "cybsp.h"
#include "cy_secure_sockets.h"
#include "cy_tls.h"

/* Wi-Fi connection manager header files */
#include "cy_wcm.h"
#include "cy_wcm_error.h"

/* Standard C header file */
#include <string.h>

/* HTTPS client task header file. */
#include "secure_http_client.h"
#include "cy_http_client_api.h"
#include "secure_keys.h"
#include "lwip/ip_addr.h"

/* FreeRTOS header file */
#include <FreeRTOS.h>
#include <task.h>
#include "retarget_io_init.h"

#include "cy_json_parser.h"
#include "comm_manager.h"
/*******************************************************************************
 *                                MACROS
 *****************************************************************************/
#define MEMSET_VAL                                   (0U)
#define HTTPS_CLIENT_TASK_DELAY_MS                   (15U * 60U * 1000U) /* 15 Minutes */
#define HTTP_CLIENT_CACHE_CLEANUP_DELAY_MS           (500U) /* 5 Minutes */
/*******************************************************************************
 *                              GLOBAL VARIABLES
 *****************************************************************************/
static cy_http_client_method_t http_client_method;

/* Secure HTTP server information. */
static cy_awsport_server_info_t server_info;

/* Buffer to store get response */
static uint8_t http_get_buffer[HTTP_GET_BUFFER_LENGTH];

/* Secure HTTP client instance. */
static cy_http_client_t https_client;

cy_http_client_request_header_t request;
cy_http_client_header_t header;
cy_http_client_response_t response;

bool time_synced = false;

static char latitude[16];   // For lat extracted from "loc"
static char longitude[16];  // For lon extracted from "loc"
static char timezone[32];   // For timezone
static char city[16];       // For City
static char timedata[32];
static char temperature[16];
static char hummidity[16];
static char windspeed[16];
static char weathercode[16];
static char formatted_time[32];

/* Handle for the HTTPS client task - used to receive wake-up notifications
 * (e.g. after factory reset) so the 15-minute periodic delay can be cut short. */
TaskHandle_t https_task_handle = NULL;

/*******************************************************************************
 *                          FUNCTION PROTOTYPES
 *****************************************************************************/
static void fetch_https_client_method(void);
static void disconnect_callback_handler(cy_http_client_t handle,
                                 cy_http_client_disconn_type_t type, void *args);
static cy_rslt_t send_http_request(cy_http_client_t handle,
                            cy_http_client_method_t method,const char * pPath);
static cy_rslt_t configure_https_client(const char *host_name, uint16_t port);
static void delete_http_client();
void url_encode_component(const char *in, char *out, size_t out_size);

void parse_json_location_payload(const char* payload, uint32_t payload_len);
void parse_json_weather_payload(const char* payload, uint32_t payload_len);
void parse_json_time_payload(const char* payload, uint32_t payload_len);
/*******************************************************************************
 *                          FUNCTION DEFINITIONS
 *****************************************************************************/

/*******************************************************************************
* Function Name: delete_http_client
********************************************************************************
* Summary:
*  Disconnects and deletes the global HTTPS client instance.
*
* Parameters:
*  void
*
* Return:
*  void
*
*******************************************************************************/
static void delete_http_client()
{    /* Clean up any existing client */
    if (https_client != NULL)
    {
        cy_http_client_disconnect(https_client);
        cy_http_client_delete(https_client);
        https_client = NULL;

        vTaskDelay(pdMS_TO_TICKS(HTTP_CLIENT_CACHE_CLEANUP_DELAY_MS));
    }
}

/*******************************************************************************
* Function Name: disconnect_callback
********************************************************************************
* Summary:
*  Callback function for http disconnect.
*******************************************************************************/
static void disconnect_callback_handler(cy_http_client_t handle,
        cy_http_client_disconn_type_t type, void *args)
{
    printf("\nApplication Disconnect callback triggered for handle = "
            "%p type=%d\n", handle, type);

    time_synced = false;
}
/*******************************************************************************
* Function Name: send_http_request
********************************************************************************
* Summary:
*  The function handles an http send operation.
*
* Parameters:
*  void
*
* Return:
*  cy_rslt_t: Returns CY_RSLT_SUCCESS if the secure HTTP client is configured
*  successfully, otherwise, it returns CY_RSLT_TYPE_ERROR.
*
*******************************************************************************/
static cy_rslt_t send_http_request( cy_http_client_t handle,
        cy_http_client_method_t method, const char * pPath)
{
    /* Return value of all methods from the HTTP Client library API. */
    cy_rslt_t http_status = CY_RSLT_SUCCESS;

   /* Initialize the response object. The same buffer used for storing
    * request headers is reused here.
    */
    request.buffer = http_get_buffer;
    request.buffer_len = HTTP_GET_BUFFER_LENGTH;
    request.headers_len = HTTP_REQUEST_HEADER_LEN;
    request.method = method;
    request.range_end = HTTP_REQUEST_RANGE_END;
    request.range_start = HTTP_REQUEST_RANGE_START;
    request.resource_path = pPath;
    header.field = "Connection";
    header.field_len = strlen("Connection");
    header.value = "keep-alive";
    header.value_len = strlen("keep-alive");

    http_status = cy_http_client_write_header(handle, &request, &header, NUM_HTTP_HEADERS);
    if(CY_RSLT_SUCCESS != http_status)
    {
        printf("\nWrite Header ----------- Fail \n");
        return http_status;
    }
    else
    {
        printf( "\n Sending Request Headers:\n%.*s\n",( int ) request.headers_len, ( char * ) request.buffer);
    }

    http_status = cy_http_client_send(handle, &request, (uint8_t *)REQUEST_BODY, REQUEST_BODY_LENGTH, &response);
    if(CY_RSLT_SUCCESS != http_status)
    {
        printf("\nFailed to send HTTP method=%d\n Error=%ld\r\n",request.method,(unsigned long)http_status);
    }
    else
    {
    }

    return http_status;
}

/*******************************************************************************
* Function Name: configure_https_client
********************************************************************************
* Summary:
*  Configures the security parameters such as client certificate, private key,
*  and the root CA certificate to start the HTTP client in secure mode.
*
* Parameters:
*  void
*
* Return:
*  cy_rslt_t: Returns CY_RSLT_SUCCESS if the secure HTTP client is configured
*  successfully, otherwise, it returns CY_RSLT_TYPE_ERROR.
*
*******************************************************************************/
static cy_rslt_t configure_https_client(const char *host_name, uint16_t port)
{
    cy_rslt_t result = CY_RSLT_SUCCESS;
    cy_http_disconnect_callback_t http_cb;

    if (host_name == NULL)
    {
        ERR_INFO(("host_name is NULL\n"));
        return CY_RSLT_TYPE_ERROR;
    }
    
    /* Clear existing server_info structure. */
    (void)memset(&server_info, MEMSET_VAL, sizeof(server_info));

    /* Set the server hostname and port dynamically. */
    server_info.host_name = host_name;
    server_info.port = port;

    /* Set the disconnect callback handler (if needed). */
    http_cb = disconnect_callback_handler;

    /* Create an instance of the HTTP client for the given server information. */
    result = cy_http_client_create(NULL, &server_info, http_cb, NULL, &https_client);
    if (CY_RSLT_SUCCESS != result)
    {
        ERR_INFO(("Failed to create http client.\n"));
    }

    return result;
}

/*******************************************************************************
* Function Name: https_client_task
********************************************************************************
* Summary:
*  Starts the HTTP client in secure mode. This example application is using a
*  self-signed certificate which means there is no third-party certificate issuing
*  authority involved in the authentication of the client. It is the user's
*  responsibility to supply the necessary security configurations such as client's
*  certificate, private key of the client, and RootCA of the client to start the
*  HTTP client in secure mode.
*
* Parameters:
*  arg - Unused.
*
* Return:
*  None.
*
*******************************************************************************/
void https_client_task(void *arg)
{
    cy_rslt_t result = CY_RSLT_SUCCESS;

    /* Save task handle so other modules (e.g. factory reset) can wake us up */
    https_task_handle = xTaskGetCurrentTaskHandle();

    /* Initialize the HTTP Client Library. */
    result = cy_http_client_init();
    if (CY_RSLT_SUCCESS != result)
    {
        ERR_INFO(("Failed to initialize http client.\n"));
        vTaskDelete(NULL);
    }
    
    while(true)
    {
        if (cy_wcm_is_connected_to_ap())
        {
            // Execute HTTP operations (fetch geolocation, weather data & timezone)
            fetch_https_client_method();

            /* Wait for the periodic interval OR until another task wakes us
             * (factory-reset notifies this task so the next fetch
             *  happens immediately after Wi-Fi reconnects instead of
             *  waiting up to 15 minutes). */
            ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(HTTPS_CLIENT_TASK_DELAY_MS));
        }
        else
        {
            // Wait or delay to avoid fast loop when Wi-Fi is disconnected
            vTaskDelay(pdMS_TO_TICKS(5000));  // Retry every 5 seconds
        }
    }
}

/*******************************************************************************
* Function Name: fetch_https_client_method
********************************************************************************
* Summary:
*  The function handles an http methods.
*******************************************************************************/
static void fetch_https_client_method(void)
{
    cy_rslt_t result = CY_RSLT_TYPE_ERROR;
    http_client_method = CY_HTTP_CLIENT_METHOD_GET;
    
    /* Signal UI that HTTP data fetching is starting - this sets all dashes and starts all animations */
    signal_http_fetching_start();
    vTaskDelay(pdMS_TO_TICKS(100));
    
    /*********************************************** Step 1: Fetch geolocation data *******************************************/
    /* Location data will be synced via IPC_CMD_LOCATION_SYNC when received */
    
    printf("\nConfiguring client for Geolocation API (ipinfo.io)...\n");
    result = configure_https_client(GEO_SERVER_HOST, GEO_PORT);
    if (CY_RSLT_SUCCESS != result) {
        ERR_INFO(("Failed to configure HTTP client for geolocation API.\n"));
        return;
    }

    /* Connect the HTTP client to the geolocation server */
    result = cy_http_client_connect(https_client, TRANSPORT_SEND_RECV_TIMEOUT_MS, TRANSPORT_SEND_RECV_TIMEOUT_MS);
    if (CY_RSLT_SUCCESS != result) {
        ERR_INFO(("Failed to connect to the geolocation server.\n"));
        return;
    }

    printf("\nFetching geolocation data from ipinfo.io...\n");

    result = send_http_request(https_client, http_client_method, GEO_PATH);
    if (CY_RSLT_SUCCESS == result) {
        printf("\nSuccessfully received geolocation response. Parsing JSON...\n");
        // Parse the received JSON
        parse_json_location_payload((const char *)response.body, response.body_len);
    } else {
        ERR_INFO(("Failed to fetch geolocation data.\n"));
        return;
    }
    delete_http_client();
     
    /*********************************************** Step 2: Fetch weather data ***********************************************/
    /* Weather data will be synced via IPC_CMD_WEATHER_SYNC when received */
    
    char weather_path[256] = {0};
    snprintf(weather_path, sizeof(weather_path),
             "/v1/forecast?latitude=%s&longitude=%s&current=temperature_2m,relative_humidity_2m,wind_speed_10m,weather_code", latitude, longitude);

    printf("\nFetching weather data from Open-Meteo...\n");
    result = configure_https_client(WEATHER_SERVER_HOST, WEATHER_PORT);
    if (CY_RSLT_SUCCESS != result) {
        ERR_INFO(("Failed to configure HTTP client for weather API.\n"));
        return;
    }

    result = cy_http_client_connect(https_client, TRANSPORT_SEND_RECV_TIMEOUT_MS, TRANSPORT_SEND_RECV_TIMEOUT_MS);
    if (CY_RSLT_SUCCESS != result) {
        ERR_INFO(("Failed to connect to weather server.\n"));
        return;
    }

    printf("\nSending request to Weather API...\n");
    result = send_http_request(https_client, http_client_method, weather_path);
    if (CY_RSLT_SUCCESS != result) {
        ERR_INFO(("Failed to fetch weather data.\n"));
    }
    else{
        parse_json_weather_payload((const char *)response.body, response.body_len);
    }

    delete_http_client();

    /*********************************************** Step 3: Fetch timezone data **********************************************/
    /* Date/time data will be synced via individual IPC_CMD_*_SYNC messages when received */
    
    static bool timeSynced = false;

    if(!timeSynced) 
    {
        char timezone_path[256] = {0};
        snprintf(timezone_path, sizeof(timezone_path),
                "/v2.1/get-time-zone?key=OASF0WKZLRVD&format=json&by=position&lat=%s&lng=%s", latitude, longitude);

        printf("\nFetching timezone data from timezonedb...\n");
        result = configure_https_client(TIMEZONE_SERVER_HOST, TIMEZONE_PORT);
        if (CY_RSLT_SUCCESS != result) {
            ERR_INFO(("Failed to configure HTTP client for timezone API.\n"));
            return;
        }

        result = cy_http_client_connect(https_client, TRANSPORT_SEND_RECV_TIMEOUT_MS, TRANSPORT_SEND_RECV_TIMEOUT_MS);
        if (CY_RSLT_SUCCESS != result) {
            ERR_INFO(("Failed to connect to timezone server.\n"));
            return;
        }

        printf("\nSending request to timezone API...\n");
        result = send_http_request(https_client, http_client_method, timezone_path);
        if (CY_RSLT_SUCCESS != result) {
            ERR_INFO(("Failed to fetch timezone data.\n"));
        }
        else{
            parse_json_time_payload((const char *)response.body, response.body_len);
        }

        delete_http_client();

        timeSynced = true; // Set flag to avoid re-syncing time on next loop iteration
    }
}

/*******************************************************************************
* Function Name: map_codepoint_to_ascii
********************************************************************************
* Summary:
*  Maps a Unicode codepoint (Latin diacritics) to its closest ASCII equivalent.
*
* Parameters:
*  cp - The Unicode codepoint to map.
*
* Return:
*  char - The ASCII equivalent, or 0 if no mapping exists.
*
*******************************************************************************/
static char map_codepoint_to_ascii(uint32_t cp)
{
    switch (cp)
    {
        // Macron vowels (Latin Extended-A)
        case 0x0100: return 'A'; // Ā
        case 0x0101: return 'a'; // ā
        case 0x012A: return 'I'; // Ī
        case 0x012B: return 'i'; // ī
        case 0x016A: return 'U'; // Ū
        case 0x016B: return 'u'; // ū
        case 0x014C: return 'O'; // Ō
        case 0x014D: return 'o'; // ō
        case 0x0112: return 'E'; // Ē
        case 0x0113: return 'e'; // ē

        // Acute S (ś) may appear as 0x015B/0x015A (Latin Extended-A)
        case 0x015A: return 'S'; // Ś
        case 0x015B: return 's'; // ś

        // Dot below letters (Latin Extended Additional)
        case 0x1E0C: return 'D'; // Ḍ
        case 0x1E0D: return 'd'; // ḍ
        case 0x1E6C: return 'T'; // Ṭ
        case 0x1E6D: return 't'; // ṭ
        case 0x1E46: return 'N'; // Ṇ
        case 0x1E47: return 'n'; // ṇ
        case 0x1E5A: return 'R'; // Ṛ
        case 0x1E5B: return 'r'; // ṛ
        case 0x1E62: return 'S'; // Ṣ
        case 0x1E63: return 's'; // ṣ

        // Tilde-N (not common for Indian place names, but included)
        case 0x00D1: return 'N'; // Ñ (Latin-1)
        case 0x00F1: return 'n'; // ñ

        default: return 0; // no mapping; let caller decide
    }
}

/*******************************************************************************
* Function Name: transliterate_utf8_to_ascii
********************************************************************************
* Summary:
*  Decodes a UTF-8 string and transliterates mapped diacritics to ASCII.
*
* Parameters:
*  in - Input UTF-8 string.
*  out - Output ASCII buffer.
*  out_size - Size of output buffer.
*
* Return:
*  size_t - Number of bytes written (excluding null terminator).
*
*******************************************************************************/
static size_t transliterate_utf8_to_ascii(const char* in, char* out, size_t out_size)
{
    if (!in || !out || out_size == 0) return 0;

    size_t oi = 0;
    for (size_t i = 0; in[i] != '\0'; )
    {
        unsigned char c = (unsigned char)in[i];

        if (c < 0x80) {
            // ASCII byte: copy directly
            if (oi + 1 < out_size) out[oi++] = (char)c;
            i++;
        }
        else if ((c & 0xE0) == 0xC0 && in[i+1] != '\0') {
            // 2-byte UTF-8
            unsigned char c2 = (unsigned char)in[i+1];
            uint32_t cp = ((uint32_t)(c & 0x1F) << 6) | (uint32_t)(c2 & 0x3F);
            char mapped = map_codepoint_to_ascii(cp);
            if (mapped) {
                if (oi + 1 < out_size) out[oi++] = mapped;
            } else {
                if (oi + 1 < out_size) out[oi++] = '?';
            }
            i += 2;
        }
        else if ((c & 0xF0) == 0xE0 && in[i+1] != '\0' && in[i+2] != '\0') {
            // 3-byte UTF-8
            unsigned char c2 = (unsigned char)in[i+1];
            unsigned char c3 = (unsigned char)in[i+2];
            uint32_t cp = ((uint32_t)(c & 0x0F) << 12) |
                          ((uint32_t)(c2 & 0x3F) << 6)  |
                          (uint32_t)(c3 & 0x3F);
            char mapped = map_codepoint_to_ascii(cp);
            if (mapped) {
                if (oi + 1 < out_size) out[oi++] = mapped;
            } else {
                if (oi + 1 < out_size) out[oi++] = '?';
            }
            i += 3;
        }
        else {
            // Fallback for unexpected sequences
            if (oi + 1 < out_size) out[oi++] = '?';
            i++;
        }
    }

    if (oi < out_size) out[oi] = '\0';
    return oi;
}

/*******************************************************************************
* Function Name: url_encode_component
********************************************************************************
* Summary:
*  Percent-encodes a string for use in a URL query component.
*
* Parameters:
*  in - Input string to encode.
*  out - Output buffer for encoded string.
*  out_size - Size of output buffer.
*
* Return:
*  void
*
*******************************************************************************/
void url_encode_component(const char *in, char *out, size_t out_size)
{
    static const char hex[] = "0123456789ABCDEF";
    size_t oi = 0;
    for (size_t i = 0; in[i] != '\0' && oi + 3 < out_size; i++) {
        unsigned char c = (unsigned char)in[i];
        if ((c >= 'A' && c <= 'Z') ||
            (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') ||
            c == '-' || c == '_' || c == '.' || c == '~') {
            out[oi++] = c;
        } else if (c == ' ') {
            out[oi++] = '%';
            out[oi++] = '2';
            out[oi++] = '0';
        } else {
            out[oi++] = '%';
            out[oi++] = hex[c >> 4];
            out[oi++] = hex[c & 0x0F];
        }
    }
    out[oi] = '\0';
}

/*****************************************************************************************************************************************************/
/* JSON Process for Location */
/*****************************************************************************************************************************************************/

/*******************************************************************************
* Function Name: json_location_callback
********************************************************************************
* Summary:
*  JSON parser callback that extracts location, timezone, and city fields.
*
* Parameters:
*  json_object - Pointer to parsed JSON object.
*  arg - User argument (unused).
*
* Return:
*  cy_rslt_t - CY_RSLT_SUCCESS on success.
*
*******************************************************************************/
cy_rslt_t json_location_callback(cy_JSON_object_t* json_object, void* arg) 
{
    if (json_object == NULL) {
        printf("Error: NULL JSON object in callback!\n");
        return CY_RSLT_JSON_GENERIC_ERROR;
    }

    if (strncmp(json_object->object_string, "loc", json_object->object_string_length) == 0 &&
        strlen("loc") == json_object->object_string_length) {

        // Parse "loc" value, e.g., "12.9719,77.5937"
        char temp[64] = {0};
        snprintf(temp, sizeof(temp), "%.*s", json_object->value_length, json_object->value);

        char* separator = strchr(temp, ',');
        if (separator != NULL) {
            *separator = '\0';  // Null-terminate latitude
            snprintf(latitude, sizeof(latitude), "%s", temp);
            snprintf(longitude, sizeof(longitude), "%s", separator + 1);  // Parse longitude
            printf("Extracted -> Latitude: %s, Longitude: %s\n", latitude, longitude);
        } else {
            printf("Error: Could not split 'loc' into latitude and longitude!\n");
        }
    }
    else if (strncmp(json_object->object_string, "timezone", json_object->object_string_length) == 0 &&
             strlen("timezone") == json_object->object_string_length) {
        snprintf(timezone, sizeof(timezone), "%.*s",
                 (int)json_object->value_length, json_object->value);
        printf("Extracted -> Timezone: %s\n", timezone);
    }
    else if (strncmp(json_object->object_string, "city", json_object->object_string_length) == 0 &&
             strlen("city") == json_object->object_string_length) {
        snprintf(city, sizeof(city), "%.*s",
                 (int)json_object->value_length, json_object->value);
        printf("Extracted -> city: %s\n", city);

        char city_ascii[64];
        transliterate_utf8_to_ascii(city, city_ascii, sizeof(city_ascii));
        printf("Transliterated -> city (ASCII): %s\n", city_ascii);

        set_location_sync(city_ascii);
    }
    else {
    }

    return CY_RSLT_SUCCESS;
}

/*******************************************************************************
* Function Name: parse_json_location_payload
********************************************************************************
* Summary:
*  Parses a geolocation JSON payload using the Cypress JSON parser.
*
* Parameters:
*  payload - Pointer to the JSON payload string.
*  payload_len - Length of the payload.
*
* Return:
*  void
*
*******************************************************************************/
void parse_json_location_payload(const char* payload, uint32_t payload_len)
{
    if (payload == NULL || payload_len == 0) {
        printf("Error: Payload is empty or NULL!\n");
        return;
    }

    char *valid_json = malloc(payload_len + 1);
    if (!valid_json) {
        printf("Error: malloc failed at FUN: %s!\n", __FUNCTION__);
        return;
    }
    strncpy(valid_json, payload, payload_len);
    valid_json[payload_len] = '\0';

    cy_rslt_t reg_result = cy_JSON_parser_register_callback(json_location_callback, NULL);
    if (reg_result != CY_RSLT_SUCCESS) {
        printf("Error: Failed to register JSON callback! Error: %ld\n", (long)reg_result);
        free(valid_json);
        return;
    }

    cy_rslt_t result = cy_JSON_parser(valid_json, payload_len);
    if (result != CY_RSLT_SUCCESS) {
        printf("Error: Failed to parse JSON payload! Error: %ld\n", (long)result);
        free(valid_json);
        return;
    }

    free(valid_json);
}
/*****************************************************************************************************************************************************/
/* JSON Process for Weather */
/*****************************************************************************************************************************************************/

/*******************************************************************************
* Function Name: json_weather_cb
********************************************************************************
* Summary:
*  JSON parser callback that extracts weather data fields.
*
* Parameters:
*  object - Pointer to parsed JSON object.
*  arg - User argument (unused).
*
* Return:
*  cy_rslt_t - CY_RSLT_SUCCESS on success.
*
*******************************************************************************/
static cy_rslt_t json_weather_cb(cy_JSON_object_t *object, void *arg)
{
    if(object == NULL || object->object_string == NULL)
        return CY_RSLT_SUCCESS;

    if (strncmp(object->object_string, "time", object->object_string_length) == 0 &&
        object->value_type == JSON_STRING_TYPE)
    {
        snprintf(timedata, sizeof(timedata), "%.*s",
                 (int)object->value_length, object->value);
    }
    else if (strncmp(object->object_string, "temperature_2m", object->object_string_length) == 0)
    {
        snprintf(temperature, sizeof(temperature), "%.*s",
                 (int)object->value_length, object->value);
    }
    else if (strncmp(object->object_string, "relative_humidity_2m", object->object_string_length) == 0)
    {
        snprintf(hummidity, sizeof(hummidity), "%.*s",
                 (int)object->value_length, object->value);
    }
    else if (strncmp(object->object_string, "wind_speed_10m", object->object_string_length) == 0)
    {
        snprintf(windspeed, sizeof(windspeed), "%.*s",
                 (int)object->value_length, object->value);
    }
    else if (strncmp(object->object_string, "weather_code", object->object_string_length) == 0)
    {
        snprintf(weathercode, sizeof(weathercode), "%.*s",
                 (int)object->value_length, object->value);
    }

    return CY_RSLT_SUCCESS;
}

/*******************************************************************************
* Function Name: parse_json_weather_payload
********************************************************************************
* Summary:
*  Parses a weather JSON payload and syncs data via IPC.
*
* Parameters:
*  payload - Pointer to the JSON payload string.
*  payload_len - Length of the payload.
*
* Return:
*  void
*
*******************************************************************************/
void parse_json_weather_payload(const char* payload, uint32_t payload_len)
{
    if (payload == NULL || payload_len == 0) {
        printf("Error: Payload is NULL or empty!\n");
        return;
    }

    char *json_buf = malloc(payload_len + 1);
    if (!json_buf) {
        printf("Error: malloc failed at FUN: %s!\n", __FUNCTION__);
        return;
    }
    memcpy(json_buf, payload, payload_len);
    json_buf[payload_len] = '\0';

    cy_rslt_t reg_result = cy_JSON_parser_register_callback(json_weather_cb, NULL);
    if (reg_result != CY_RSLT_SUCCESS) {
        printf("Error: Failed to register JSON callback! Error: %ld\n", (long)reg_result);
        free(json_buf);
        return;
    }

    cy_rslt_t result = cy_JSON_parser(json_buf, payload_len);

    if (result == CY_RSLT_SUCCESS)
    {
        printf("Temperature: %s �C\n", temperature);

        vTaskDelay(pdMS_TO_TICKS(50)); 
        set_weather_sync(temperature);
        vTaskDelay(pdMS_TO_TICKS(50)); 
        set_weather_code_sync(weathercode);
    }
    else
    {
        printf("JSON parsing failed! Error: 0x%08lX\n", (long)result);
    }

    free(json_buf);
}

/*****************************************************************************************************************************************************/
/* JSON Process for Timezone */
/*****************************************************************************************************************************************************/

/*******************************************************************************
* Function Name: json_time_cb
********************************************************************************
* Summary:
*  JSON parser callback that extracts the formatted timestamp string.
*
* Parameters:
*  object - Pointer to parsed JSON object.
*  arg - User argument (unused).
*
* Return:
*  cy_rslt_t - CY_RSLT_SUCCESS on success.
*
*******************************************************************************/
static cy_rslt_t json_time_cb(cy_JSON_object_t *object, void *arg)
{
    if (object == NULL || object->object_string == NULL)
        return CY_RSLT_SUCCESS;

    if (object->object_string_length == (sizeof("formatted") - 1) &&
        strncmp(object->object_string, "formatted", object->object_string_length) == 0 &&
        object->value_type == JSON_STRING_TYPE)
    {
        // Ensure proper bounds and null-termination
        size_t n = (object->value_length < sizeof(formatted_time) - 1)
                    ? object->value_length
                    : sizeof(formatted_time) - 1;
        memcpy(formatted_time, object->value, n);
        formatted_time[n] = '\0';
    }

    return CY_RSLT_SUCCESS;
}

/*******************************************************************************
* Function Name: parse_formatted_to_components
********************************************************************************
* Summary:
*  Parses a formatted time string into individual date/time components.
*
* Parameters:
*  fmt - Formatted time string ("YYYY-MM-DD HH:MM:SS").
*  year - Pointer to store parsed year.
*  month - Pointer to store parsed month.
*  day - Pointer to store parsed day.
*  hour - Pointer to store parsed hour.
*  minute - Pointer to store parsed minute.
*  second - Pointer to store parsed second.
*
* Return:
*  int - 0 on success, negative value on error.
*
*******************************************************************************/
static int parse_formatted_to_components(const char* fmt,
                                         int* year, int* month, int* day,
                                         int* hour, int* minute, int* second)
{
    if (fmt == NULL) return -1;

    // Prefer sscanf for simplicity; width specifiers constrain the read.
    // Example: "2025-10-27 15:10:59"
    int y, m, d, hh, mm, ss;
    int n = sscanf(fmt, "%4d-%2d-%2d %2d:%2d:%2d", &y, &m, &d, &hh, &mm, &ss);
    if (n != 6) {
        return -2;
    }

    // Basic range checks
    if (m < 1 || m > 12) return -3;
    if (d < 1 || d > 31) return -4;
    if (hh < 0 || hh > 23) return -5;
    if (mm < 0 || mm > 59) return -6;
    if (ss < 0 || ss > 59) return -7;

    *year = y; *month = m; *day = d;
    *hour = hh; *minute = mm; *second = ss;
    return 0;
}

/*******************************************************************************
* Function Name: weekday_from_date
********************************************************************************
* Summary:
*  Computes the day-of-week from a Gregorian date using Sakamoto's algorithm.
*
* Parameters:
*  y - Year.
*  m - Month (1-12).
*  d - Day (1-31).
*
* Return:
*  int - Day of week (0=Sun, 1=Mon, ..., 6=Sat).
*
*******************************************************************************/
static int weekday_from_date(int y, int m, int d)
{
    static const int t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
    if (m < 3) y -= 1;
    int w = (y + y/4 - y/100 + y/400 + t[m-1] + d) % 7;
    return w;
}

/*******************************************************************************
* Function Name: parse_json_time_payload
********************************************************************************
* Summary:
*  Parses a timezone JSON response and syncs time components via IPC.
*
* Parameters:
*  payload - Pointer to the JSON payload string.
*  payload_len - Length of the payload.
*
* Return:
*  void
*
*******************************************************************************/
void parse_json_time_payload(const char* payload, uint32_t payload_len)
{
    if(!time_synced)
    {
        if (payload == NULL || payload_len == 0) {
            printf("Error: Payload is NULL or empty!\n");
            return;
        }

        char *json_buf = malloc(payload_len + 1);
        if (!json_buf) {
            printf("Error: malloc failed at FUN: %s!\n", __FUNCTION__);
            return;
        }
        memcpy(json_buf, payload, payload_len);
        json_buf[payload_len] = '\0';

        cy_rslt_t reg_result = cy_JSON_parser_register_callback(json_time_cb, NULL);
        if (reg_result != CY_RSLT_SUCCESS) {
            printf("Error: Failed to register JSON callback! Error: %ld\n", (long)reg_result);
            free(json_buf);
            return;
        }

        cy_rslt_t result = cy_JSON_parser(json_buf, payload_len);

        if (result == CY_RSLT_SUCCESS)
        {
            printf("Time(Local): %s\n", formatted_time);
        }
        else
        {
            printf("JSON parsing failed! Error: 0x%08lX\n", (long)result);
        }

        if (formatted_time[0] == '\0') {
            printf("Error: formatted time is missing!\n");
            free(json_buf);
            return;
        }

        int year, month, day, hour, minute, second;
        if (parse_formatted_to_components(formatted_time, &year, &month, &day, &hour, &minute, &second) != 0) {
            printf("Error: failed to parse formatted time: '%s'\n", formatted_time);
            free(json_buf);
            return;
        }

        // Zero-padded strings
        char hour_str[3], min_str[3], sec_str[3], day_str[3], month_str[3], year_str[5];

        snprintf(hour_str, sizeof(hour_str),  "%02d", hour);
        snprintf(min_str,  sizeof(min_str),   "%02d", minute);
        snprintf(sec_str,  sizeof(sec_str),   "%02d", second);
        snprintf(day_str,  sizeof(day_str),   "%02d", day);
        snprintf(month_str,sizeof(month_str), "%02d", month);
        snprintf(year_str, sizeof(year_str),  "%04d", year);

        // Sync in requested order with delays
        set_hour_sync(hour_str);
        vTaskDelay(pdMS_TO_TICKS(300));

        set_minute_sync(min_str);
        vTaskDelay(pdMS_TO_TICKS(300));

        set_second_sync(sec_str);
        vTaskDelay(pdMS_TO_TICKS(300));

        // Weekday sync
        const char* weekdays[] = {"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};
        int wday = weekday_from_date(year, month, day);
        set_vaar_sync(weekdays[wday]);
        vTaskDelay(pdMS_TO_TICKS(250));

        set_year_sync(year_str);
        vTaskDelay(pdMS_TO_TICKS(300));

        set_month_sync(month_str);
        vTaskDelay(pdMS_TO_TICKS(300));

        set_date_sync(day_str);
        vTaskDelay(pdMS_TO_TICKS(300));

        free(json_buf);
        
        time_synced = true;
    }
}

/*****************************************************************************************************************************************************/
/* [] END OF FILE */
/*****************************************************************************************************************************************************/
