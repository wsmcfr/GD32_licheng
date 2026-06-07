#include "rtc_app.h"

/* 每月天数（非闰年），索引 0=1月 … 11=12月 */
static const uint8_t s_days_per_month[12] = {31,28,31,30,31,30,31,31,30,31,30,31};

static uint8_t is_leap(uint16_t year)
{
    return ((year % 4 == 0) && ((year % 100 != 0) || (year % 400 == 0))) ? 1 : 0;
}

/* Unix时间戳转年月日时分秒，格里高利历快速推算 */
static void ts_to_dt(uint32_t ts, bsp_rtc_datetime_t *dt)
{
    uint32_t days = ts / 86400, tod = ts % 86400;
    uint32_t year = 1970, n400, n100, n4, n1, month, day;

    dt->hour   = (uint8_t)(tod / 3600);
    dt->minute = (uint8_t)((tod % 3600) / 60);
    dt->second = (uint8_t)(tod % 60);
    dt->day_of_week = 0;

    n400 = days / 146097; days -= n400 * 146097; year += n400 * 400;
    n100 = days / 36524;  if(n100 > 3) { n100 = 3; }
    days -= n100 * 36524; year += n100 * 100;
    n4   = days / 1461;   days -= n4 * 1461;   year += n4 * 4;
    n1   = days / 365;    if(n1 > 3) { n1 = 3; }
    days -= n1 * 365;     year += n1;

    for(month = 1; month <= 12; month++) 
	{
        uint32_t dim = s_days_per_month[month - 1];
        if(month == 2 && is_leap((uint16_t)year)) dim = 29;
        if(days < dim) break;
        days -= dim;
    }
    day = days + 1;

    dt->year  = (uint16_t)year;
    dt->month = (uint8_t)month;
    dt->date  = (uint8_t)day;
}

/* 年月日时分秒转Unix时间戳 */
static uint32_t dt_to_ts(const bsp_rtc_datetime_t *dt)
{
    uint32_t days = 0;
    uint16_t y;
    uint8_t m;

    for(y = 1970; y < dt->year; y++)
        days += 365 + (is_leap(y) ? 1 : 0);

    for(m = 1; m < dt->month; m++) 
	{
        days += s_days_per_month[m - 1];
        if(m == 2 && is_leap(dt->year)) days++;
    }

    days += dt->date - 1;

    return days * 86400 + (uint32_t)dt->hour   * 3600 + (uint32_t)dt->minute * 60 + (uint32_t)dt->second;
}

/* 每秒更新RTC共享缓存 */
void rtc_task(void)
{
    rtc_current_time_get(&rtc_initpara);
}

int rtc_app_set_unix_epoch(uint32_t unix_ts)
{
    bsp_rtc_datetime_t dt;
    ts_to_dt(unix_ts, &dt);
    return bsp_rtc_set_datetime(&dt);
}

int rtc_app_get_unix_epoch(uint32_t *unix_ts)
{
    bsp_rtc_datetime_t dt;

    if(!unix_ts) return -1;
    if(bsp_rtc_get_datetime(&dt) != 0) return -1;

    *unix_ts = dt_to_ts(&dt);
    return 0;
}
