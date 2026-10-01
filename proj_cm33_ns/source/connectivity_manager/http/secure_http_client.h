/*******************************************************************************
* File Name: secure_http_client.h
*
* Description: This file contains configuration parameters for the secure HTTP
* client along with the HTML page that the client will host.
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

#ifndef SECURE_HTTP_CLIENT_H_
#define SECURE_HTTP_CLIENT_H_


#ifdef __cplusplus
extern "C" {
#endif
/*******************************************************************************
 *                                INCLUDES
 *****************************************************************************/
#include "FreeRTOS.h"
#include <task.h>
#include "cy_wcm.h"
#include "cybsp.h"
#include "cy_network_mw_core.h"

/*******************************************************************************
 *                                MACROS
 *****************************************************************************/
#define ERR_INFO(x)                do { printf("Error: "); printf x; } while(0);

/* IP-based location service (plain HTTP, no API key). */
#define LOCATION_SERVER_HOST                        "ipwho.is"
#define LOCATION_SERVER_PORT                        (80)
#define LOCATION_SERVER_PATH                        "/"

/* Weather sync is skipped while WEATHER_SERVER_HOST is the placeholder. Set the
 * host and path to your weather service (the path takes latitude then longitude)
 * and adapt json_weather_cb() in secure_http_client.c to its response. */
#define WEATHER_SERVER_HOST_PLACEHOLDER             "<your-weather-service-host>"
#define WEATHER_SERVER_HOST                         WEATHER_SERVER_HOST_PLACEHOLDER
#define WEATHER_SERVER_PORT                         (80)
#define WEATHER_PATH_FORMAT                         "/<your-weather-api-path>?latitude=%s&longitude=%s"

#define TRANSPORT_SEND_RECV_TIMEOUT_MS           (10000)
#define HTTP_GET_BUFFER_LENGTH                   (2048 * 4)
#define REQUEST_BODY                             (NULL)
#define REQUEST_BODY_LENGTH                      ( 0 )

/* End Range until where the data is expected. Set to -1 if requested
 * range is all bytes from the starting
 */
#define HTTP_REQUEST_RANGE_END                   (-1)

/* Start Range from where the server should return. */
#define HTTP_REQUEST_RANGE_START                 (0U)

/* Number of headers in the header list */
#define NUM_HTTP_HEADERS                         (1U)

/* Length of the request header. */
#define HTTP_REQUEST_HEADER_LEN                  (0U)

/*******************************************************************************
 *                          FUNCTION PROTOTYPES
 *****************************************************************************/
void https_client_task(void *arg);

/* Task handle - used by other modules to wake the HTTPS task early */
extern TaskHandle_t https_task_handle;

#ifdef __cplusplus
}
#endif

#endif /* SECURE_HTTP_CLIENT_H_ */


/* [] END OF FILE */
