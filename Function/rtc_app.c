#include "rtc_app.h"

/* 每月天数（非闰年），索引 0=1月 … 11=12月。 */
static const uint8_t s_days_per_month[12] = {31U,28U,31U,30U,31U,30U,31U,31U,30U,31U,30U,31U};

/*
 * 函数作用：
 *   判断指定年份是否为格里高利历闰年。
 * 参数说明：
 *   year：待判断的完整年份（如 2026）。
 * 返回值说明：
 *   1：是闰年。
 *   0：不是闰年。
 */
static uint8_t prv_rtc_is_leap_year(uint16_t year)
{
    return ((year % 4U == 0U) && ((year % 100U != 0U) || (year % 400U == 0U))) ? 1U : 0U;
}

/*
 * 函数作用：
 *   将 Unix UTC 秒级时间戳转换为年月日时分秒结构体。
 * 主要流程：
 *   1. 用 400/100/4/1 年格里高利历周期迭代推算年份，避免逐年循环耗时。
 *   2. 从年内剩余天数推算月份，考虑闰年 2 月 29 天。
 *   3. 日期和时间部分直接整除取余即可。
 * 参数说明：
 *   unix_ts：Unix 纪元（1970-01-01 00:00:00 UTC）起的秒数。
 *   dt：输出时间结构体；调用方保证指针非空。
 * 返回值说明：
 *   无返回值。
 */
static void prv_unix_ts_to_datetime(uint32_t unix_ts, bsp_rtc_datetime_t *dt)
{
    uint32_t days  = unix_ts / 86400UL;
    uint32_t tod   = unix_ts % 86400UL;
    uint32_t year  = 1970UL;
    uint32_t n400, n100, n4, n1;
    uint32_t month, day;

    dt->hour   = (uint8_t)(tod / 3600UL);
    dt->minute = (uint8_t)((tod % 3600UL) / 60UL);
    dt->second = (uint8_t)(tod % 60UL);
    dt->day_of_week = 0U;  /* bsp_rtc_set_datetime 会自动计算 */

    /*
     * 格里高利历周期：
     *   400 年 = 146097 天（97 个闰年）
     *   100 年 = 36524 天（24 个闰年）
     *   4 年   = 1461 天（1 个闰年）
     *   1 年   = 365 天
     * 每段周期末尾不足一个完整周期时限制最大值，避免越界。
     */
    n400 = days / 146097UL; days -= n400 * 146097UL; year += n400 * 400UL;
    n100 = days / 36524UL;  if(n100 > 3UL) { n100 = 3UL; }
    days -= n100 * 36524UL; year += n100 * 100UL;
    n4   = days / 1461UL;   days -= n4 * 1461UL;   year += n4 * 4UL;
    n1   = days / 365UL;    if(n1 > 3UL) { n1 = 3UL; }
    days -= n1 * 365UL;     year += n1;

    /* 从年内剩余天数（0-based）推算月份和日期。 */
    for(month = 1UL; month <= 12UL; month++) {
        uint32_t dim = (uint32_t)s_days_per_month[month - 1UL];
        if((month == 2UL) && (0U != prv_rtc_is_leap_year((uint16_t)year))) {
            dim = 29UL;
        }
        if(days < dim) {
            break;
        }
        days -= dim;
    }
    day = days + 1UL;

    dt->year  = (uint16_t)year;
    dt->month = (uint8_t)month;
    dt->date  = (uint8_t)day;
}

/*
 * 函数作用：
 *   将年月日时分秒结构体转换为 Unix UTC 秒级时间戳。
 * 主要流程：
 *   1. 从 1970 年起逐年累加天数（考虑各闰年）。
 *   2. 在当年中按月累加，考虑本年是否为闰年对二月的影响。
 *   3. 加入当月日期偏移和当天秒数。
 * 参数说明：
 *   dt：输入时间结构体；调用方保证指针非空且年份 >= 1970。
 * 返回值说明：
 *   返回 Unix 纪元起的秒数。
 */
static uint32_t prv_datetime_to_unix_ts(const bsp_rtc_datetime_t *dt)
{
    uint32_t days = 0UL;
    uint16_t y;
    uint8_t  m;

    /* 累计 1970-01-01 到 dt->year-01-01 的天数。 */
    for(y = 1970U; y < dt->year; y++) {
        days += 365UL + ((0U != prv_rtc_is_leap_year(y)) ? 1UL : 0UL);
    }

    /* 累计当年 1 月到 dt->month-1 月的天数。 */
    for(m = 1U; m < dt->month; m++) {
        days += (uint32_t)s_days_per_month[m - 1U];
        if((m == 2U) && (0U != prv_rtc_is_leap_year(dt->year))) {
            days++;
        }
    }

    /* 加入当月日偏移（1-based → 0-based）。 */
    days += (uint32_t)(dt->date - 1U);

    return days * 86400UL
           + (uint32_t)dt->hour   * 3600UL
           + (uint32_t)dt->minute * 60UL
           + (uint32_t)dt->second;
}

/*
 * 函数作用：
 *   周期性读取 RTC 当前时间并刷新驱动层共享时间缓存。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   无返回值。
 */
void rtc_task(void)
{
    rtc_current_time_get(&rtc_initpara);
}

/*
 * 函数作用：
 *   将 Unix UTC 秒级时间戳写入 RTC。
 *   内部先转换为年月日时分秒，再调用驱动层 bsp_rtc_set_datetime()。
 * 参数说明：
 *   unix_ts：Unix 纪元（1970-01-01 00:00:00 UTC）起的秒数。
 * 返回值说明：
 *   0：写入成功。
 *  -1：驱动层写入失败。
 */
int rtc_app_set_unix_epoch(uint32_t unix_ts)
{
    bsp_rtc_datetime_t dt;

    prv_unix_ts_to_datetime(unix_ts, &dt);

    return bsp_rtc_set_datetime(&dt);
}

/*
 * 函数作用：
 *   读取 RTC 当前时间并转换为 Unix UTC 秒级时间戳。
 *   内部先调用驱动层 bsp_rtc_get_datetime()，再做时区中立的算术转换。
 * 参数说明：
 *   unix_ts：输出指针，成功时写入 Unix 纪元起的秒数；为空时直接返回 -1。
 * 返回值说明：
 *   0：读取并转换成功。
 *  -1：参数为空或 RTC 读取失败。
 */
int rtc_app_get_unix_epoch(uint32_t *unix_ts)
{
    bsp_rtc_datetime_t dt;

    if(NULL == unix_ts) {
        return -1;
    }

    if(0 != bsp_rtc_get_datetime(&dt)) {
        return -1;
    }

    *unix_ts = prv_datetime_to_unix_ts(&dt);

    return 0;
}
