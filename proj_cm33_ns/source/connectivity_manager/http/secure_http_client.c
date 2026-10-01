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
#define HTTP_CONNECT_MAX_RETRIES                     (3U)
#define HTTP_CONNECT_RETRY_DELAY_MS                  (500U)
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

static char latitude[16];
static char longitude[16];
/* Sized to match the IPC string field (ipc_msg_t.char_value) */
static char city[32];
static bool location_received = false;
static char temperature[16];
static char weathercode[16];
static bool weather_received = false;
static char weather_server_host[] = WEATHER_SERVER_HOST;

/* Handle for the HTTPS client task - used to receive wake-up notifications
 * (e.g. after factory reset) so the 15-minute periodic delay can be cut short. */
TaskHandle_t https_task_handle = NULL;

/*******************************************************************************
 *                          FUNCTION PROTOTYPES
 *****************************************************************************/
static bool fetch_https_client_method(void);
static void disconnect_callback_handler(cy_http_client_t handle,
                                 cy_http_client_disconn_type_t type, void *args);
static cy_rslt_t send_http_request(cy_http_client_t handle,
                            cy_http_client_method_t method,const char * pPath);
static cy_rslt_t configure_https_client(const char *host_name, uint16_t port);
static cy_rslt_t http_client_connect_with_retry(cy_http_client_t handle);
static void delete_http_client();

void parse_json_location_payload(const char* payload, uint32_t payload_len);
void parse_json_weather_payload(const char* payload, uint32_t payload_len);
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
* Function Name: http_client_connect_with_retry
********************************************************************************
* Summary:
*  Connects the HTTP client to its configured server, retrying a few times
*  with a short delay to ride out transient failures (e.g. right after a
*  Wi-Fi reconnect, before DNS/routing has fully settled).
*
* Parameters:
*  handle - The HTTP client instance to connect.
*
* Return:
*  cy_rslt_t: Returns CY_RSLT_SUCCESS on success, else the last error code.
*
*******************************************************************************/
static cy_rslt_t http_client_connect_with_retry(cy_http_client_t handle)
{
    cy_rslt_t result = CY_RSLT_TYPE_ERROR;
    uint8_t retries = 0;

    do
    {
        result = cy_http_client_connect(handle, TRANSPORT_SEND_RECV_TIMEOUT_MS, TRANSPORT_SEND_RECV_TIMEOUT_MS);
        if (CY_RSLT_SUCCESS == result)
        {
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(HTTP_CONNECT_RETRY_DELAY_MS));
    } while (++retries < HTTP_CONNECT_MAX_RETRIES);

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
            // Execute HTTP operations (fetch location and, if enabled, weather data)
            if (!fetch_https_client_method())
            {
                signal_http_fetch_failed();
            }

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
*  Fetches location and, if enabled, weather data and syncs them to the UI.
*
* Return:
*  bool - true if all enabled data was synced, false otherwise.
*******************************************************************************/
static bool fetch_https_client_method(void)
{
    cy_rslt_t result = CY_RSLT_TYPE_ERROR;
    http_client_method = CY_HTTP_CLIENT_METHOD_GET;
    
    /* Signal UI that HTTP data fetching is starting - this sets all dashes and starts all animations */
    signal_http_fetching_start();
    vTaskDelay(pdMS_TO_TICKS(100));
    
    /*********************************************** Step 1: Fetch location data *********************************************/
    /* Location data will be synced via IPC_CMD_LOCATION_SYNC when received */
    
    printf("\nConfiguring HTTP client for location service...\n");
    result = configure_https_client(LOCATION_SERVER_HOST, LOCATION_SERVER_PORT);
    if (CY_RSLT_SUCCESS != result) {
        ERR_INFO(("Failed to configure HTTP client for location service.\n"));
        return false;
    }

    /* Connect the HTTP client to the location server */
    result = http_client_connect_with_retry(https_client);
    if (CY_RSLT_SUCCESS != result) {
        ERR_INFO(("Failed to connect to the location server.\n"));
        delete_http_client();
        return false;
    }

    printf("\nFetching location data...\n");

    location_received = false;
    result = send_http_request(https_client, http_client_method, LOCATION_SERVER_PATH);
    if (CY_RSLT_SUCCESS == result) {
        printf("\nSuccessfully received location response. Parsing JSON...\n");
        // Parse the received JSON
        parse_json_location_payload((const char *)response.body, response.body_len);
    } else {
        ERR_INFO(("Failed to fetch location data.\n"));
        delete_http_client();
        return false;
    }
    delete_http_client();

    if (!location_received) {
        ERR_INFO(("Location not found in the response.\n"));
        return false;
    }

    /*********************************************** Step 2: Fetch weather data ***********************************************/
    /* Weather data will be synced via IPC_CMD_WEATHER_SYNC when received */
    if (0 == strcmp(weather_server_host, WEATHER_SERVER_HOST_PLACEHOLDER))
    {
        printf("\nWeather service not configured: set WEATHER_SERVER_HOST in secure_http_client.h. Skipping weather sync.\n");
        return true;
    }

    char weather_path[256] = {0};
    snprintf(weather_path, sizeof(weather_path), WEATHER_PATH_FORMAT, latitude, longitude);

    printf("\nFetching weather data...\n");
    result = configure_https_client(weather_server_host, WEATHER_SERVER_PORT);
    if (CY_RSLT_SUCCESS != result) {
        ERR_INFO(("Failed to configure HTTP client for weather service.\n"));
        return false;
    }

    result = http_client_connect_with_retry(https_client);
    if (CY_RSLT_SUCCESS != result) {
        ERR_INFO(("Failed to connect to weather server.\n"));
        delete_http_client();
        return false;
    }

    printf("\nSending request to weather service...\n");
    weather_received = false;
    result = send_http_request(https_client, http_client_method, weather_path);
    if (CY_RSLT_SUCCESS != result) {
        ERR_INFO(("Failed to fetch weather data.\n"));
    }
    else{
        parse_json_weather_payload((const char *)response.body, response.body_len);
    }

    delete_http_client();

    if (!weather_received) {
        return false;
    }

    return true;
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

/*****************************************************************************************************************************************************/
/* JSON Process for Location */
/*****************************************************************************************************************************************************/

/*******************************************************************************
* Function Name: json_location_callback
********************************************************************************
* Summary:
*  JSON parser callback that extracts the latitude, longitude, and city fields.
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

    if (strncmp(json_object->object_string, "latitude", json_object->object_string_length) == 0 &&
        strlen("latitude") == json_object->object_string_length) {
        snprintf(latitude, sizeof(latitude), "%.*s",
                 (int)json_object->value_length, json_object->value);
    }
    else if (strncmp(json_object->object_string, "longitude", json_object->object_string_length) == 0 &&
             strlen("longitude") == json_object->object_string_length) {
        snprintf(longitude, sizeof(longitude), "%.*s",
                 (int)json_object->value_length, json_object->value);
        printf("Extracted -> Latitude: %s, Longitude: %s\n", latitude, longitude);
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
        location_received = true;
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
*  JSON parser callback that extracts weather data fields. The keys below are an
*  example; adapt them to your weather service's response. The weather code is
*  expected as a WMO weather interpretation code.
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

    if (strncmp(object->object_string, "temperature_2m", object->object_string_length) == 0)
    {
        snprintf(temperature, sizeof(temperature), "%.*s",
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
        printf("Temperature: %s °C\n", temperature);

        vTaskDelay(pdMS_TO_TICKS(50)); 
        set_weather_sync(temperature);
        vTaskDelay(pdMS_TO_TICKS(50)); 
        set_weather_code_sync(weathercode);
        weather_received = true;
    }
    else
    {
        printf("JSON parsing failed! Error: 0x%08lX\n", (long)result);
    }

    free(json_buf);
}

/*****************************************************************************************************************************************************/
/* [] END OF FILE */
/*****************************************************************************************************************************************************/
