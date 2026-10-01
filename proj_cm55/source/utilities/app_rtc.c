/*******************************************************************************
 * File Name:  app_rtc.c
 *
 * Description: Real-time clock (RTC) control module implementation.
 *
 * This file implements the RTC control functions, including initialization,
 * time synchronization using a FreeRTOS timer, and manual time updates.
 *
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
 *                              INCLUDES
 ******************************************************************************/
#include "app_rtc.h"
#include "app_gfx_disp.h"
#include "timers.h"

/*******************************************************************************
 *                              CONSTANTS
 ******************************************************************************/
#define READ_RTC_DATA_MS        1000U   /* Read RTC time update interval */
#define RTC_ADDRESS             (0x6F)

#define REG_RTCSEC              (0x00) /* Time Second (bit7 = ST) */
#define REG_RTCMIN              (0x01) /* Time Minute */
#define REG_RTCHOUR             (0x02) /* Time Hour (bit6 = 12/24 hour) */
#define REG_RTCWKDAY            (0x03) /* Day-of-Week (bit3 = VBATEN, bit4 = PWRFAIL) */
#define REG_RTCDATE             (0x04) /* Day */
#define REG_RTCMTH              (0x05) /* Month */
#define REG_RTCYEAR             (0x06) /* Year (00..99) */
#define REG_CONTROL             (0x07) /* Control */
#define REG_OSCTRIM             (0x08) /* Oscillator digital trim */
#define REG_PWRDNMIN            (0x18) /* Power Failure Time Minute */
#define REG_PWRDNHR             (0x19) /* Power Failure Time Hour */
#define REG_PWRUPMIN            (0x1C) /* Power Restore Time Minute */
#define REG_PWRUPHR             (0x1D) /* Power Restore Time Hour */

#define I2C_TIMEOUT_MS          (50)
#define RTC_SRAM_MAGIC_ADDR  0x20 /* start of battery-backed SRAM */
#define RTC_SRAM_MAGIC_LEN   4
/*******************************************************************************
 *                              GLOBAL VARIABLES
 ******************************************************************************/
volatile bool update_timestamp = false;
cy_stc_rtc_config_t ui_current_time;
const cy_stc_sysint_t rtc_intr_config = { .intrSrc = srss_interrupt_rtc_IRQn, .intrPriority = 3, };

/*******************************************************************************
 *                              STATIC VARIABLES
 ******************************************************************************/
static TimerHandle_t minute_sync_timer = NULL;
static bool rtc_init_state = false;

static const uint8_t rtc_magic[RTC_SRAM_MAGIC_LEN] = { 'I','F','X','R' };

/****************************************************************************
 *                              FUNCTION DECLARATIONS
 ***************************************************************************/

/*******************************************************************************
 * Function Name: minute_sync_freertos_timer_cb
 ********************************************************************************
 * Summary:
 * FreeRTOS timer callback for minute synchronization. Notifies the graphics
 * task to update the UI and resets the timer for the next minute.
 *
 * Parameter:
 * xTimer: A handle to the FreeRTOS timer.
 *
 * Return :
 * void
 *******************************************************************************/
static void minute_sync_freertos_timer_cb(TimerHandle_t xTimer);

/*******************************************************************************
 * Function Name: start_minute_sync_timer
 ********************************************************************************
 * Summary:
 * Initializes and starts the minute synchronization timer.
 *
 * Parameter:
 * void
 *
 * Return :
 * void
 *******************************************************************************/
void start_minute_sync_timer(void);

/*******************************************************************************
 * Function Name: stop_minute_sync_timer
 ********************************************************************************
 * Summary:
 * Stops the minute synchronization timer.
 *
 * Parameter:
 * void
 *
 * Return :
 * void
 *******************************************************************************/
void stop_minute_sync_timer(void);

/*******************************************************************************
 * Function Name: validate_date_time
 ********************************************************************************
 * Summary:
 * This function validates date and time value.
 *
 * Parameters:
 * int sec     : The second valid range is [0-59].
 * int min     : The minute valid range is [0-59].
 * int hour    : The hour valid range is [0-23].
 * int mday    : The day of month valid range is [1-31], if the month of February
 * is selected as the Month parameter, then the valid range
 * is [0-29].
 * int month   : The month valid range is [1-12].
 * int year    : The year valid range is [> 0].
 *
 * Return:
 * false - invalid ; true - valid
 *
 *******************************************************************************/
static bool validate_date_time(int sec, int min, int hour, int mday, int month, int year);

/*******************************************************************************
 *                              FUNCTION DEFINITIONS
 ******************************************************************************/

/*******************************************************************************
* Function Name: minute_sync_freertos_timer_cb
********************************************************************************
* Summary:
*  FreeRTOS timer callback for minute synchronization. Notifies graphics
*  task to update the UI and resets the timer.
*
* Parameters:
*  xTimer - Handle to the FreeRTOS timer.
*
* Return:
*  void
*
*******************************************************************************/
static void minute_sync_freertos_timer_cb(TimerHandle_t xTimer)
{
    /* Notify the graphics task to update the UI. */
    update_timestamp = true;

    /* Read current date and time */
    Cy_RTC_GetDateAndTime(&ui_current_time);

    /* After the initial one-shot, we set the timer to fire again in 1 seconds.
     This creates a continuous, per-minute update loop. */
    xTimerChangePeriod(xTimer, pdMS_TO_TICKS(READ_RTC_DATA_MS), 0);
}

/*******************************************************************************
* Function Name: start_minute_sync_timer
********************************************************************************
* Summary:
*  Creates and starts the minute synchronization one-shot timer.
*
* Parameters:
*  void
*
* Return:
*  void
*
*******************************************************************************/
void start_minute_sync_timer(void)
{
    /* The timer is set to be a one-shot initially (pdFALSE). */
    minute_sync_timer = xTimerCreate("MinuteSyncTimer", pdMS_TO_TICKS(READ_RTC_DATA_MS), pdFALSE, (void*) 0,
            minute_sync_freertos_timer_cb);

    /* Start the timer */
    if (minute_sync_timer != NULL)
    {
        xTimerStart(minute_sync_timer, 0);
    }
}

/*******************************************************************************
* Function Name: stop_minute_sync_timer
********************************************************************************
* Summary:
*  Stops and deletes the minute synchronization timer.
*
* Parameters:
*  void
*
* Return:
*  void
*
*******************************************************************************/
void stop_minute_sync_timer(void)
{
    if (minute_sync_timer != NULL)
    {
        /* Stop and delete the timer. */
        xTimerStop(minute_sync_timer, 100);
        xTimerDelete(minute_sync_timer, 100);
        minute_sync_timer = NULL;
    }
}

/*******************************************************************************
* Function Name: validate_date_time
********************************************************************************
* Summary:
*  Validates date and time values against their valid ranges.
*
* Parameters:
*  sec   - Seconds (0-59).
*  min   - Minutes (0-59).
*  hour  - Hours (0-23).
*  mday  - Day of month (1-31).
*  month - Month (1-12).
*  year  - Year (> 0).
*
* Return:
*  bool - true if valid, false otherwise.
*
*******************************************************************************/
static bool validate_date_time(int sec, int min, int hour, int mday, int month, int year)
{
    static const uint8_t days_in_month_table[MONTHS_PER_YEAR] = {
        DAYS_IN_JANUARY,
        DAYS_IN_FEBRUARY,
        DAYS_IN_MARCH,
        DAYS_IN_APRIL,
        DAYS_IN_MAY,
        DAYS_IN_JUNE,
        DAYS_IN_JULY,
        DAYS_IN_AUGUST,
        DAYS_IN_SEPTEMBER,
        DAYS_IN_OCTOBER,
        DAYS_IN_NOVEMBER,
        DAYS_IN_DECEMBER,
    };

    uint8_t days_in_month;

    bool rslt = IS_SEC_VALID(sec) & IS_MIN_VALID(min) &
    IS_HOUR_VALID(hour) & IS_MONTH_VALID(month) &
    IS_YEAR_VALID(year);

    if (rslt)
    {
        days_in_month = days_in_month_table[month - 1];

        if (IS_LEAP_YEAR(year) && (month == 2))
        {
            days_in_month++;
        }

        rslt &= (mday > 0U) && (mday <= days_in_month);
    }

    return rslt;
}

/*******************************************************************************
* Function Name: app_rtc_init
********************************************************************************
* Summary:
*  Initializes the RTC module, sets default time, and starts sync timer.
*
* Parameters:
*  void
*
* Return:
*  void
*
*******************************************************************************/
void app_rtc_init(void)
{
    Cy_RTC_Init(&CYBSP_RTC_config);

    cy_stc_rtc_config_t current_time = { .sec = 0, .min = 30, .hour = 17, .hrFormat = CY_RTC_24_HOURS, .dayOfWeek =
            CY_RTC_MONDAY, .date = 27, .month = 10, .year = 25 };

    /* Set current time */
    Cy_RTC_SetDateAndTime(&current_time);

    /* Read current datetime info */
    Cy_RTC_GetDateAndTime(&ui_current_time);

    /* Start minute sync timer */
    start_minute_sync_timer();

    /* Update state */
    rtc_init_state = true;
    update_timestamp = true;
}

/*******************************************************************************
* Function Name: update_time_on_ui
********************************************************************************
* Summary:
*  Reads current time from RTC and updates the UI timestamp.
*
* Parameters:
*  void
*
* Return:
*  void
*
*******************************************************************************/
void update_time_on_ui(void)
{
    if (true == rtc_init_state)
    {
        /* Read current date time info */
        Cy_RTC_GetDateAndTime(&ui_current_time);
    }
}

/*******************************************************************************
* Function Name: set_date_time_rtc
********************************************************************************
* Summary:
*  Sets the RTC date and time from a DateTime structure and restarts sync.
*
* Parameters:
*  info - Pointer to DateTime structure with new date/time values.
*
* Return:
*  void
*
*******************************************************************************/
void set_date_time_rtc(DateTime *info)
{
    cy_stc_rtc_config_t new_rtc_config;

    if (!validate_date_time(info->second, info->minute, info->hour,
                            info->day, info->month, info->year))
    {
        LOG_ERROR(CYLF_DEF, "set_date_time_rtc: invalid date/time, ignoring update.\n");
        return;
    }

    /* Read the current RTC configuration to preserve settings like hrFormat. */
    Cy_RTC_GetDateAndTime(&new_rtc_config);

    new_rtc_config.year = (info->year - RTC_CENTURY);
    new_rtc_config.month = info->month;
    new_rtc_config.date = info->day;
    new_rtc_config.hour = info->hour;
    new_rtc_config.min = info->minute;
    new_rtc_config.sec = info->second;

    /* Calculate the day of the week from the date */
    new_rtc_config.dayOfWeek = Cy_RTC_ConvertDayOfWeek(
        new_rtc_config.date,
        new_rtc_config.month,
        (new_rtc_config.year + RTC_CENTURY));
    printf("Calculated day of the week: %lu\r\n", (unsigned long)new_rtc_config.dayOfWeek);

    /* Now set the new date and time on the RTC. */
    Cy_RTC_SetDateAndTime(&new_rtc_config);

    /* Read values from the RTC to update the global UI timestamp */
    Cy_RTC_GetDateAndTime(&ui_current_time);

    /* Restart UI update timer */
    stop_minute_sync_timer();
    start_minute_sync_timer();
    update_timestamp = true;
}

/*******************************************************************************
* Function Name: rtc_init
********************************************************************************
* Summary:
*  Initializes the external MCP7940N RTC over I2C.
*
* Parameters:
*  void
*
* Return:
*  cy_rslt_t - CY_RSLT_SUCCESS on success.
*
*******************************************************************************/
cy_rslt_t rtc_init(void)
{
    cy_rslt_t rslt;

    /* Optional settle delay to avoid early bus NAKs */
    Cy_SysLib_Delay(20);

    /* If rtc oscillator is already running, do nothing more */
    if (rtc_is_running()) {
        return CY_RSLT_SUCCESS;
    }

    /* Enable VBATEN for power-fail detection */
    rslt = rtc_configure_bit(REG_RTCWKDAY, 3, 0x01);
    if (rslt != CY_RSLT_SUCCESS) return rslt;

    /* 24-hour mode (optional). Keep only if you must enforce 24h. */
    rslt = rtc_configure_bit(REG_RTCHOUR, 6, 0x00);
    if (rslt != CY_RSLT_SUCCESS) return rslt;

    /* Start oscillator: set ST=1 in seconds register */
    rslt = rtc_configure_bit(REG_RTCSEC, 7, 0x01);
    if (rslt != CY_RSLT_SUCCESS) return rslt;

    return CY_RSLT_SUCCESS;
}

/*******************************************************************************
* Function Name: to_bcd
********************************************************************************
* Summary:
*  Converts a decimal value to BCD format.
*
* Parameters:
*  dec - Decimal value to convert.
*
* Return:
*  uint8_t - BCD encoded value.
*
*******************************************************************************/
static uint8_t to_bcd(uint8_t dec)   { return ((dec / 10) << 4) | (dec % 10); }

/*******************************************************************************
* Function Name: from_bcd
********************************************************************************
* Summary:
*  Converts a BCD value to decimal format.
*
* Parameters:
*  bcd - BCD value to convert.
*
* Return:
*  uint8_t - Decimal value.
*
*******************************************************************************/
static uint8_t from_bcd(uint8_t bcd) { return ((bcd >> 4) * 10) + (bcd & 0x0F); }

/*******************************************************************************
* Function Name: rtc_configure_bit
********************************************************************************
* Summary:
*  Reads an RTC register, modifies a single bit, and writes it back.
*
* Parameters:
*  reg       - Register address.
*  positions - Bit position to modify.
*  value     - Bit value (0 or 1).
*
* Return:
*  cy_rslt_t - CY_RSLT_SUCCESS on success.
*
*******************************************************************************/
cy_rslt_t rtc_configure_bit(uint8_t reg, uint8_t positions, uint8_t value)
{
    uint8_t regval = 0;
    cy_rslt_t rslt = rtc_read_register(reg, &regval);
    if (rslt != CY_RSLT_SUCCESS) return rslt;

    if (value) regval |=  (1u << positions);
    else       regval &= ~(1u << positions);

    return rtc_write_register(reg, regval);
}

/*******************************************************************************
* Function Name: rtc_write_register
********************************************************************************
* Summary:
*  Writes a single byte to an RTC register.
*
* Parameters:
*  reg   - Register address.
*  value - Value to write.
*
* Return:
*  cy_rslt_t - CY_RSLT_SUCCESS on success.
*
*******************************************************************************/
cy_rslt_t rtc_write_register(uint8_t reg, uint8_t value)
{
    return rtc_write_registers(reg, &value, 1);
}

/*******************************************************************************
* Function Name: rtc_read_register
********************************************************************************
* Summary:
*  Reads a single byte from an RTC register.
*
* Parameters:
*  reg   - Register address.
*  value - Pointer to store the read value.
*
* Return:
*  cy_rslt_t - CY_RSLT_SUCCESS on success.
*
*******************************************************************************/
cy_rslt_t rtc_read_register(uint8_t reg, uint8_t* value)
{
    return rtc_read_registers(reg, value, 1);
}

/*******************************************************************************
* Function Name: rtc_write_registers
********************************************************************************
* Summary:
*  Writes multiple bytes to consecutive RTC registers over I2C.
*
* Parameters:
*  start_reg - Starting register address.
*  data      - Pointer to data buffer.
*  len       - Number of bytes to write.
*
* Return:
*  cy_rslt_t - CY_RSLT_SUCCESS on success.
*
*******************************************************************************/
cy_rslt_t rtc_write_registers(uint8_t start_reg, const uint8_t* data, size_t len)
{
    /* Prepare a tx buffer: [reg][data...] */
    uint8_t temp[1 + 16]; /* adjust if you need larger writes */
    if (len > 16) return CY_RSLT_TYPE_ERROR;
    temp[0] = start_reg;
    for (size_t i = 0; i < len; ++i) temp[1 + i] = data[i];

    /* Write with a STOP */
    return mtb_hal_i2c_controller_write(&CYBSP_I2C_CONTROLLER_hal_obj,
                                    RTC_ADDRESS,
                                    temp, (1 + len),
                                    I2C_TIMEOUT_MS, true);
}

/*******************************************************************************
* Function Name: rtc_read_registers
********************************************************************************
* Summary:
*  Reads multiple bytes from consecutive RTC registers over I2C.
*
* Parameters:
*  start_reg - Starting register address.
*  data      - Pointer to buffer to store read data.
*  len       - Number of bytes to read.
*
* Return:
*  cy_rslt_t - CY_RSLT_SUCCESS on success.
*
*******************************************************************************/
cy_rslt_t rtc_read_registers(uint8_t start_reg, uint8_t* data, size_t len)
{
    cy_rslt_t rslt;

    /* Write the register pointer without STOP to allow repeated-start */
    rslt = mtb_hal_i2c_controller_write(&CYBSP_I2C_CONTROLLER_hal_obj,
                                    RTC_ADDRESS,
                                    &start_reg, 1,
                                    I2C_TIMEOUT_MS, false);
    if (rslt != CY_RSLT_SUCCESS) return rslt;

    /* Now read data with STOP */
    rslt = mtb_hal_i2c_controller_read(&CYBSP_I2C_CONTROLLER_hal_obj,
                                   RTC_ADDRESS,
                                   data, len,
                                   I2C_TIMEOUT_MS, true);
    return rslt;
}

/*******************************************************************************
* Function Name: rtc_set_time
********************************************************************************
* Summary:
*  Sets the RTC time by writing all 7 time/date registers.
*
* Parameters:
*  t - Pointer to rtc_time_t structure with time values.
*
* Return:
*  cy_rslt_t - CY_RSLT_SUCCESS on success.
*
*******************************************************************************/
cy_rslt_t rtc_set_time(rtc_time_t* t)
{
    if (!t) return CY_RSLT_TYPE_ERROR;
    cy_rslt_t rslt;
    uint8_t buf[7];

    buf[0] = to_bcd(t->seconds) & 0x7F; /* ST=0 while writing */
    buf[1] = to_bcd(t->minutes) & 0x7F;
    buf[2] = to_bcd(t->hours)   & 0x3F; /* 24-hour mode */
    buf[3] = to_bcd(t->dow)     & 0x07; /* VBATEN will be re-set below */
    buf[4] = to_bcd(t->day)     & 0x3F;
    buf[5] = to_bcd(t->month)   & 0x1F;
    buf[6] = to_bcd(t->year);

    rslt = rtc_write_registers(REG_RTCSEC, buf, 7);
    if (rslt != CY_RSLT_SUCCESS) return rslt;

    /* Re-enable VBATEN (WKDAY bit3) because writing WKDAY clobbers control bits */
    rslt = rtc_configure_bit(REG_RTCWKDAY, 3, 0x01);
    if (rslt != CY_RSLT_SUCCESS) return rslt;

    /* Start oscillator */
    rslt = rtc_configure_bit(REG_RTCSEC, 7, 0x01);
    return rslt;
}

/*******************************************************************************
* Function Name: rtc_set_time_safe
********************************************************************************
* Summary:
*  Sets the RTC time safely by stopping oscillator first, writing fields
*  individually, then restarting oscillator.
*
* Parameters:
*  t - Pointer to rtc_time_t structure with time values.
*
* Return:
*  cy_rslt_t - CY_RSLT_SUCCESS on success.
*
*******************************************************************************/
cy_rslt_t rtc_set_time_safe(const rtc_time_t* t)
{
    if (!t) return CY_RSLT_TYPE_ERROR;
    cy_rslt_t r;

    /* 1) Clear ST while loading seconds (write seconds with bit7=0) */
    uint8_t sec = to_bcd(t->seconds) & 0x7F;
    r = rtc_write_register(REG_RTCSEC, sec);
    if (r != CY_RSLT_SUCCESS) return r;

    /* 2) Write other fields individually (do NOT touch WKDAY here) */
    r = rtc_write_register(REG_RTCMIN,  to_bcd(t->minutes) & 0x7F); if (r != CY_RSLT_SUCCESS) return r;
    r = rtc_write_register(REG_RTCHOUR, to_bcd(t->hours)   & 0x3F); if (r != CY_RSLT_SUCCESS) return r; /* 24-hour mode */
    r = rtc_write_register(REG_RTCDATE, to_bcd(t->day)     & 0x3F); if (r != CY_RSLT_SUCCESS) return r;
    r = rtc_write_register(REG_RTCMTH,  to_bcd(t->month)   & 0x1F); if (r != CY_RSLT_SUCCESS) return r;
    r = rtc_write_register(REG_RTCYEAR, to_bcd(t->year));           if (r != CY_RSLT_SUCCESS) return r;

    /* 3) Update WKDAY with the new DOW, preserving VBATEN (bit3) and letting PWRFAIL clear as per spec */
    uint8_t wkday_old = 0;
    r = rtc_read_register(REG_RTCWKDAY, &wkday_old);
    if (r != CY_RSLT_SUCCESS) return r;

    uint8_t new_wkday = (wkday_old & 0xF8) | (to_bcd(t->dow) & 0x07); /* keep bit3 (VBATEN), clear bit4 by write, preserve bit5 status */
    r = rtc_write_register(REG_RTCWKDAY, new_wkday);
    if (r != CY_RSLT_SUCCESS) return r;

    /* 4) Start oscillator by setting ST bit */
    r = rtc_configure_bit(REG_RTCSEC, 7, 0x01);
    return r;
}

/*******************************************************************************
* Function Name: rtc_get_time
********************************************************************************
* Summary:
*  Reads the current time from the RTC.
*
* Parameters:
*  t - Pointer to rtc_time_t structure to store the read time.
*
* Return:
*  cy_rslt_t - CY_RSLT_SUCCESS on success.
*
*******************************************************************************/
cy_rslt_t rtc_get_time(rtc_time_t* t)
{
    if (!t) return CY_RSLT_TYPE_ERROR;
    uint8_t buf[7];
    cy_rslt_t rslt = rtc_read_registers(REG_RTCSEC, buf, 7);
    if (rslt != CY_RSLT_SUCCESS) return rslt;

    t->seconds = from_bcd(buf[0] & 0x7F);
    t->minutes = from_bcd(buf[1] & 0x7F);
    t->hours   = from_bcd(buf[2] & 0x3F);
    t->dow     = from_bcd(buf[3] & 0x07);
    t->day     = from_bcd(buf[4] & 0x3F);
    t->month   = from_bcd(buf[5] & 0x1F);
    t->year    = from_bcd(buf[6]);

    return CY_RSLT_SUCCESS;
}

/*******************************************************************************
* Function Name: rtc_clear_power_fail_flag
********************************************************************************
* Summary:
*  Clears the power fail flag (WKDAY bit4) in the RTC.
*
* Parameters:
*  void
*
* Return:
*  cy_rslt_t - CY_RSLT_SUCCESS on success.
*
*******************************************************************************/
cy_rslt_t rtc_clear_power_fail_flag(void)
{
    return rtc_configure_bit(REG_RTCWKDAY, 4, 0x00);
}

/*******************************************************************************
* Function Name: rtc_is_running
********************************************************************************
* Summary:
*  Checks whether the RTC oscillator is running.
*
* Parameters:
*  void
*
* Return:
*  bool - true if oscillator is running.
*
*******************************************************************************/
bool rtc_is_running(void)
{
    uint8_t sec = 0;
    if (rtc_read_register(REG_RTCSEC, &sec) != CY_RSLT_SUCCESS) return false;
    return (sec & 0x80) != 0; /* ST bit */
}

/*******************************************************************************
* Function Name: rtc_start_oscillator
********************************************************************************
* Summary:
*  Starts the RTC oscillator by setting the ST bit.
*
* Parameters:
*  void
*
* Return:
*  cy_rslt_t - CY_RSLT_SUCCESS on success.
*
*******************************************************************************/
cy_rslt_t rtc_start_oscillator(void)
{
    return rtc_configure_bit(REG_RTCSEC, 7, 0x01);
}

/*******************************************************************************
* Function Name: rtc_time_is_valid
********************************************************************************
* Summary:
*  Validates that an rtc_time_t structure contains reasonable values.
*
* Parameters:
*  t - Pointer to rtc_time_t structure to validate.
*
* Return:
*  bool - true if the time is valid.
*
*******************************************************************************/
bool rtc_time_is_valid(const rtc_time_t* t)
{
    if (!t) return false;

    /* Basic range checks for 24-hour mode and BCD-decoded fields */
    bool ranges_ok =
        (t->seconds <= 59) &&
        (t->minutes <= 59) &&
        (t->hours   <= 23) &&
        (t->dow     >= 1 && t->dow <= 7) &&
        (t->day     >= 1 && t->day <= 31) &&
        (t->month   >= 1 && t->month <= 12) &&
        (t->year    <= 99);

    /* Consider all-zero time/date as invalid */
    bool all_zero = (t->seconds == 0) && (t->minutes == 0) && (t->hours == 0) &&
                    (t->dow == 1) && (t->day == 1) && (t->month == 1) && (t->year == 1);

    return ranges_ok && !all_zero;
}

/*******************************************************************************
* Function Name: rtc_service_power_fail
********************************************************************************
* Summary:
*  Checks for and logs any power-fail events, then clears the flag.
*
* Parameters:
*  void
*
* Return:
*  cy_rslt_t - CY_RSLT_SUCCESS on success.
*
*******************************************************************************/
cy_rslt_t rtc_service_power_fail(void)
{
    cy_rslt_t r;
    uint8_t wkday = 0;
    r = rtc_read_register(REG_RTCWKDAY, &wkday);
    if (r != CY_RSLT_SUCCESS) return r;

    if ((wkday & 0x10) != 0) {
        /* Read power-down/up timestamps while the flag is still set */
        uint8_t pwrDn[2] = {0}, pwrUp[2] = {0};
        r = rtc_read_registers(REG_PWRDNMIN, pwrDn, 2);
        if (r != CY_RSLT_SUCCESS) return r;
        r = rtc_read_registers(REG_PWRUPMIN, pwrUp, 2);
        if (r != CY_RSLT_SUCCESS) return r;

        uint8_t dn_min = ((pwrDn[0] >> 4) * 10) + (pwrDn[0] & 0x0F);
        uint8_t dn_hr  = ((pwrDn[1] >> 4) * 10) + (pwrDn[1] & 0x0F);
        uint8_t up_min = ((pwrUp[0] >> 4) * 10) + (pwrUp[0] & 0x0F);
        uint8_t up_hr  = ((pwrUp[1] >> 4) * 10) + (pwrUp[1] & 0x0F);

        LOG_INFO(CYLF_DEF,"Power Drop Detected: PWRDN %02u:%02u\r\n", dn_hr, dn_min);
        LOG_INFO(CYLF_DEF,"Power On Detected: PWRUP %02u:%02u\r\n", up_hr, up_min);

        /* Clear PWRFAIL so the next event will be logged */
        r = rtc_clear_power_fail_flag();
        if (r != CY_RSLT_SUCCESS) return r;
    }
    return CY_RSLT_SUCCESS;
}

/*******************************************************************************
* Function Name: rtc_sram_write_magic
********************************************************************************
* Summary:
*  Writes the magic signature to RTC SRAM to mark initialization.
*
* Parameters:
*  void
*
* Return:
*  cy_rslt_t - CY_RSLT_SUCCESS on success.
*
*******************************************************************************/
cy_rslt_t rtc_sram_write_magic(void)
{
    return rtc_write_registers(RTC_SRAM_MAGIC_ADDR, rtc_magic, RTC_SRAM_MAGIC_LEN);
}

/*******************************************************************************
* Function Name: rtc_sram_read_magic
********************************************************************************
* Summary:
*  Reads and verifies the magic signature from RTC SRAM.
*
* Parameters:
*  present - Pointer to bool, set true if magic is found.
*
* Return:
*  cy_rslt_t - CY_RSLT_SUCCESS on success.
*
*******************************************************************************/
cy_rslt_t rtc_sram_read_magic(bool* present)
{
    if (!present) return CY_RSLT_TYPE_ERROR;
    uint8_t buf[RTC_SRAM_MAGIC_LEN] = {0};
    cy_rslt_t r = rtc_read_registers(RTC_SRAM_MAGIC_ADDR, buf, RTC_SRAM_MAGIC_LEN);
    if (r != CY_RSLT_SUCCESS) return r;
    *present = (buf[0] == rtc_magic[0] &&
                buf[1] == rtc_magic[1] &&
                buf[2] == rtc_magic[2] &&
                buf[3] == rtc_magic[3]);
    return CY_RSLT_SUCCESS;
}

/*******************************************************************************
* Function Name: dow_from_date
********************************************************************************
* Summary:
*  Computes the day of week (1=Monday..7=Sunday) from a date using
*  Zeller's congruence.
*
* Parameters:
*  year  - Full year (e.g. 2025).
*  month - Month (1-12).
*  day   - Day of month (1-31).
*
* Return:
*  uint8_t - Day of week (1=Monday..7=Sunday).
*
*******************************************************************************/
uint8_t dow_from_date(uint16_t year, uint8_t month, uint8_t day)
{
    /* Zeller�s congruence (Gregorian) adapted to produce 1..7 */
    /* Map: 1=Monday, 7=Sunday */
    int y = (month < 3) ? (year - 1) : year;
    int m = (month < 3) ? (month + 12) : month;
    int K = y % 100;
    int J = y / 100;
    int h = (day + (13*(m + 1))/5 + K + (K/4) + (J/4) + (5*J)) % 7; /* 0=Saturday, 1=Sunday, 2=Monday... */
    int d = ((h + 5) % 7) + 1; /* 1=Monday ... 7=Sunday */
    return (uint8_t)d;
}

/*******************************************************************************
* Function Name: rtc_set_time_safe_from_http
********************************************************************************
* Summary:
*  Sets the hardware RTC time safely from HTTP-provided date/time values.
*
* Parameters:
*  year   - Full year (e.g. 2025).
*  month  - Month (1-12).
*  day    - Day of month (1-31).
*  hour   - Hour (0-23).
*  minute - Minute (0-59).
*  second - Second (0-59).
*
* Return:
*  cy_rslt_t - CY_RSLT_SUCCESS on success.
*
*******************************************************************************/
cy_rslt_t rtc_set_time_safe_from_http(uint16_t year, uint8_t month, uint8_t day,
                                             uint8_t hour, uint8_t minute, uint8_t second)
{
    cy_rslt_t r;

    /* Reject clearly invalid date/time before touching the external RTC.
     * Valid inputs are unaffected. */
    if (!validate_date_time(second, minute, hour, day, month, year))
    {
        LOG_ERROR(CYLF_DEF, "rtc_set_time_safe_from_http: invalid date/time, ignoring update.\n");
        return CY_RSLT_TYPE_ERROR;
    }

    /* 1) Stop oscillator while setting seconds (write seconds with ST=0) */
    r = rtc_write_register(REG_RTCSEC, (to_bcd(second) & 0x7F));
    if (r != CY_RSLT_SUCCESS) return r;

    /* 2) Write other time/date fields individually (do NOT write WKDAY yet) */
    r = rtc_write_register(REG_RTCMIN,  (to_bcd(minute) & 0x7F)); if (r != CY_RSLT_SUCCESS) return r;
    r = rtc_write_register(REG_RTCHOUR, (to_bcd(hour)   & 0x3F)); if (r != CY_RSLT_SUCCESS) return r; /* 24h mode */
    r = rtc_write_register(REG_RTCDATE, (to_bcd(day)    & 0x3F)); if (r != CY_RSLT_SUCCESS) return r;
    r = rtc_write_register(REG_RTCMTH,  (to_bcd(month)  & 0x1F)); if (r != CY_RSLT_SUCCESS) return r;
    r = rtc_write_register(REG_RTCYEAR,  to_bcd((uint8_t)(year % 100))); if (r != CY_RSLT_SUCCESS) return r;

    /* 3) Compose WKDAY: new DOW, preserve VBATEN (bit3), writing WKDAY clears PWRFAIL by design */
    uint8_t wkday_old = 0;
    r = rtc_read_register(REG_RTCWKDAY, &wkday_old);
    if (r != CY_RSLT_SUCCESS) return r;

    uint8_t dow = dow_from_date(year, month, day); /* 1..7 */
    uint8_t new_wkday = (wkday_old & 0xF8) | (to_bcd(dow) & 0x07); /* keep VBATEN (bit3), OSCRUN (bit5) */
    new_wkday |= 0x08; /* ensure VBATEN stays set */

    r = rtc_write_register(REG_RTCWKDAY, new_wkday);
    if (r != CY_RSLT_SUCCESS) return r;

    /* 4) Start oscillator: set ST bit in seconds register */
    r = rtc_configure_bit(REG_RTCSEC, 7, 0x01);
    return r;
}

/*******************************************************************************
* Function Name: rtc_boot_once
********************************************************************************
* Summary:
*  One-time boot initialization for the external RTC. Handles power-fail
*  recovery and SRAM magic verification.
*
* Parameters:
*  void
*
* Return:
*  void
*
*******************************************************************************/
void rtc_boot_once(void)
{
    cy_rslt_t r;

    r = rtc_init();
    if (r != CY_RSLT_SUCCESS) {
        LOG_INFO(CYLF_DEF,"RTC Init failed: 0x%08lx\r\n", (unsigned long)r);
    }

    /* Ensure VBATEN is always set so power-fail detection works on warm boots */
    r = rtc_configure_bit(REG_RTCWKDAY, 3, 0x01);
    if (r != CY_RSLT_SUCCESS) {
        LOG_INFO(CYLF_DEF,"VBATEN enable failed: 0x%08lx\r\n", (unsigned long)r);
    }

    /* Service any power-fail record BEFORE any WKDAY write or time block writes */
    r = rtc_service_power_fail();
    if (r != CY_RSLT_SUCCESS) {
        LOG_INFO(CYLF_DEF,"Power-fail service failed: 0x%08lx\r\n", (unsigned long)r);
    }

    /* Check if RTC was previously initialized (SRAM signature) */
    bool initialized = false;
    r = rtc_sram_read_magic(&initialized);
    if (r != CY_RSLT_SUCCESS) {
        LOG_INFO(CYLF_DEF,"SRAM read magic failed: 0x%08lx\r\n", (unsigned long)r);
    }

    /* Read current time */
    rtc_time_t now;
    r = rtc_get_time(&now);
    if (r != CY_RSLT_SUCCESS) {
        LOG_INFO(CYLF_DEF,"RTC read failed: 0x%08lx\r\n", (unsigned long)r);
        return;
    }

    /* Decide whether to seed */
    if (!initialized || !rtc_time_is_valid(&now)) {
        rtc_time_t init_time = { .seconds=0, .minutes=52, .hours=10, .dow=7, .day=4, .month=4, .year=25 };
        r = rtc_set_time_safe(&init_time);
        if (r == CY_RSLT_SUCCESS) {
            /* Write the signature to indicate the RTC was initialized */
            (void)rtc_sram_write_magic();
            printf("RTC seeded once and oscillator started.\r\n");
        } else {
            printf("Set time failed: 0x%08lx\r\n", (unsigned long)r);
        }
    } else {
        /* Ensure oscillator is running; don't overwrite time */
        if (!rtc_is_running()) {
            r = rtc_start_oscillator();
            if (r != CY_RSLT_SUCCESS) {
                printf("Oscillator start failed: 0x%08lx\r\n", (unsigned long)r);
            }
        }
    }

    /* Final read and print */
    r = rtc_get_time(&now);
    if (r == CY_RSLT_SUCCESS) {
        LOG_INFO(CYLF_DEF,"Current Time: %02u:%02u:%02u  DOW:%u  %02u/%02u/%02u\r\n",
               now.hours, now.minutes, now.seconds, now.dow,
               now.day, now.month, now.year);
    }
}

/* [] END OF FILE */
