/*******************************************************************************
 * File Name:  app_rtc.h
 *
 * Description: Public interface for the real-time clock (RTC) control module.
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

#ifndef APP_RTC_H_
#define APP_RTC_H_


#ifdef __cplusplus
extern "C" {
#endif
/*******************************************************************************
 *                                INCLUDES
 *******************************************************************************/
#include "cybsp.h"
#include "retarget_io_init.h"
#include "FreeRTOS.h"
#include "task.h"
#include "cyabs_rtos.h"
#include "cyabs_rtos_impl.h"
#include "app_common.h"

/*******************************************************************************
 *                                CONSTANTS
 *******************************************************************************/
/* Structure tm stores years since 1900 */
#define TM_YEAR_BASE            (1900U)

#define RTC_CENTURY             (2000U)

/* Maximum value of seconds and minutes */
#define MAX_SEC_OR_MIN          (60U)

/* Maximum value of hours definition */
#define MAX_HOURS_24H           (23U)

/* Month per year definition */
#define MONTHS_PER_YEAR         (12U)

/* Days per week definition */
#define DAYS_PER_WEEK           (7U)

/* Days in month */
#define DAYS_IN_JANUARY         (31U)  /* Number of days in January */
#define DAYS_IN_FEBRUARY        (28U)  /* Number of days in February */
#define DAYS_IN_MARCH           (31U)  /* Number of days in March */
#define DAYS_IN_APRIL           (30U)  /* Number of days in April */
#define DAYS_IN_MAY             (31U)  /* Number of days in May */
#define DAYS_IN_JUNE            (30U)  /* Number of days in June */
#define DAYS_IN_JULY            (31U)  /* Number of days in July */
#define DAYS_IN_AUGUST          (31U)  /* Number of days in August */
#define DAYS_IN_SEPTEMBER       (30U)  /* Number of days in September */
#define DAYS_IN_OCTOBER         (31U)  /* Number of days in October */
#define DAYS_IN_NOVEMBER        (30U)  /* Number of days in November */
#define DAYS_IN_DECEMBER        (31U)  /* Number of days in December */


/* Macro to validate seconds parameter */
#define IS_SEC_VALID(sec) ((sec) <= (MAX_SEC_OR_MIN -1))

/* Macro to validate minutes parameters */
#define IS_MIN_VALID(min) ((min) <= (MAX_SEC_OR_MIN -1))

/* Macro to validate hour parameter */
#define IS_HOUR_VALID(hour) ((hour) <= MAX_HOURS_24H)

/* Macro to validate month parameter */
#define IS_MONTH_VALID(month) (((month) > 0U) && ((month) <= MONTHS_PER_YEAR))

/* Macro to validate the year value */
#define IS_YEAR_VALID(year) ((year) > 0U)

/* Checks whether the year passed through the parameter is leap or not */
#define IS_LEAP_YEAR(year) \
(((0U == (year % 4UL)) && (0U != (year % 100UL))) || (0U == (year % 400UL)))

/*******************************************************************************
 *                               GLOBAL VARIABLES
 *******************************************************************************/
extern const cy_stc_sysint_t rtc_intr_config;
extern volatile bool update_timestamp;
extern cy_stc_rtc_config_t ui_current_time;

typedef struct {
    uint8_t seconds; // 0..59
    uint8_t minutes; // 0..59
    uint8_t hours;   // 0..23
    uint8_t dow;     // 1..7 (arbitrary)
    uint8_t day;     // 1..31
    uint8_t month;   // 1..12
    uint8_t year;    // 0..99
} rtc_time_t;

typedef struct {
    bool   power_fail_flag;  // WKDAY bit4
    uint8_t pwr_down_min;    // BCD converted to decimal
    uint8_t pwr_down_hr;     // BCD converted to decimal (24h)
    uint8_t pwr_up_min;      // BCD converted to decimal
    uint8_t pwr_up_hr;       // BCD converted to decimal
} rtc_pwr_status_t;
/*******************************************************************************
 *                              FUNCTION PROTOTYPES
 *******************************************************************************/
void app_rtc_init(void);
void update_time_on_ui(void);
void set_date_time_rtc(DateTime *info);
void start_minute_sync_timer(void);
void stop_minute_sync_timer(void);
cy_rslt_t rtc_init(void);
cy_rslt_t rtc_set_time(rtc_time_t* t);
cy_rslt_t rtc_get_time(rtc_time_t* t);
cy_rslt_t rtc_configure_bit(uint8_t reg, uint8_t positions, uint8_t value);
cy_rslt_t rtc_write_register(uint8_t reg, uint8_t value);
cy_rslt_t rtc_read_register(uint8_t reg, uint8_t* value);
cy_rslt_t rtc_write_registers(uint8_t start_reg, const uint8_t* data, size_t len);
cy_rslt_t rtc_read_registers(uint8_t start_reg, uint8_t* data, size_t len);
cy_rslt_t rtc_clear_power_fail_flag(void);
cy_rslt_t rtc_start_oscillator(void);
bool rtc_is_running(void);
bool rtc_time_is_valid(const rtc_time_t* t);

void rtc_boot_once(void);
uint8_t dow_from_date(uint16_t year, uint8_t month, uint8_t day);

cy_rslt_t rtc_set_time_safe_from_http(uint16_t year, uint8_t month, uint8_t day,
                                             uint8_t hour, uint8_t minute, uint8_t second);

#ifdef __cplusplus
}
#endif

#endif /* APP_RTC_H_ */

/* [] END OF FILE */
