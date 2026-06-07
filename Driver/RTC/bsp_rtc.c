#include "bsp_rtc.h"

rtc_parameter_struct rtc_initpara;

static rtc_alarm_struct rtc_alarm;
static __IO uint32_t prescaler_a = 0;
static __IO uint32_t prescaler_s = 0;
static uint32_t rtcsrc_flag = 0;
static int rtc_clock_ready = 0;

// 十进制转BCD。
static uint8_t bsp_rtc_decimal_to_bcd(uint8_t value)
{
    return (uint8_t)(((value / 10) << 4) | (value % 10));
}

// BCD转十进制。
static uint8_t bsp_rtc_bcd_to_decimal(uint8_t value)
{
    return (uint8_t)((((value >> 4) & 0x0FU) * 10) + (value & 0x0FU));
}

// 判断闰年。
static uint8_t bsp_rtc_is_leap_year(uint16_t year)
{
    if((0 == (year % 400)) || ((0 == (year % 4)) && (0 != (year % 100))))
	{
        return 1;
    }

    return 0;
}

// 获取月天数。
static uint8_t bsp_rtc_get_days_in_month(uint16_t year, uint8_t month)
{
    static const uint8_t days_in_month[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

    if((month < 1) || (month > 12))
	{
        return 0;
    }

    if((2 == month) && (0 != bsp_rtc_is_leap_year(year)))
	{
        return 29;
    }

    return days_in_month[month - 1];
}

// 等振荡器稳定。
static int bsp_rtc_wait_osci_stable(rcu_osci_type_enum osci)
{
    if(SUCCESS == rcu_osci_stab_wait(osci))
	{
        return 0;
    }

    return -1;
}

// 校验日期时间范围。
static uint8_t bsp_rtc_is_valid_datetime(const bsp_rtc_datetime_t *datetime)
{
    uint8_t max_day;

    if(!datetime)
	{
        return 0;
    }

    if((datetime->year < 2000) || (datetime->year > 2099))
	{
        return 0;
    }

    if((datetime->month < 1) || (datetime->month > 12))
	{
        return 0;
    }

    max_day = bsp_rtc_get_days_in_month(datetime->year, datetime->month);
    if((0 == max_day) || (datetime->date < 1) || (datetime->date > max_day))
	{
        return 0;
    }

    if(datetime->hour > 23)
	{
        return 0;
    }

    if((datetime->minute > 59) || (datetime->second > 59))
	{
        return 0;
    }

    return 1;
}

// Sakamoto算法算星期。
static uint8_t bsp_rtc_calculate_day_of_week(uint16_t year, uint8_t month, uint8_t date)
{
    static const uint8_t month_offset[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
    uint16_t adjusted_year = year;
    uint8_t weekday_index;

    if(month < 3)
	{
        adjusted_year--;
    }

    weekday_index = (uint8_t)((adjusted_year + (adjusted_year / 4) - (adjusted_year / 100) + (adjusted_year / 400) + month_offset[month - 1] + date) % 7);

    if(0 == weekday_index)
	{
        return RTC_SUNDAY;
    }

    return weekday_index;
}

/* 冷启动写默认RTC时间。 */
static int bsp_rtc_setup(void)
{
    int ret = 0;
    uint32_t tmp_hh = 0x23U;
    uint32_t tmp_mm = 0x59U;
    uint32_t tmp_ss = 0x50U;

    (void)rtc_alarm;

    rtc_initpara.factor_asyn = prescaler_a;
    rtc_initpara.factor_syn = prescaler_s;
    rtc_initpara.year = 0x25U;
    rtc_initpara.day_of_week = RTC_SATURDAY;
    rtc_initpara.month = RTC_APR;
    rtc_initpara.date = 0x30U;
    rtc_initpara.display_format = RTC_24HOUR;
    rtc_initpara.am_pm = RTC_AM;
    rtc_initpara.hour = tmp_hh;
    rtc_initpara.minute = tmp_mm;
    rtc_initpara.second = tmp_ss;

    if (ERROR == rtc_init(&rtc_initpara))
	{
        ret = -1;
    }
	else
	{
        RTC_BKP0 = BKP_VALUE;
    }

    return ret;
}

// 检查备份域标记。
static uint8_t bsp_rtc_has_valid_backup(void)
{
    if(RTC_BKP0 == BKP_VALUE)
	{
        return 1;
    }

    return 0;
}

// 从备份域恢复当前RTC时间。
static int bsp_rtc_restore_from_backup(void)
{
    if(ERROR == rtc_register_sync_wait())
	{
        return -1;
    }

    rtc_current_time_get(&rtc_initpara);
    return 0;
}

/*
 * IRC32K备份域尝试迁回LXTAL，保留原时间快照。
 */
static int bsp_rtc_try_restore_lxtal_from_irc32k(uint8_t *has_valid_backup)
{
    rtc_parameter_struct saved_time;
    uint8_t saved_time_valid = 0;

    if(!has_valid_backup)
	{
        return -1;
    }

    if(2 != rtcsrc_flag)
	{
        return 0;
    }

    rcu_osci_on(RCU_LXTAL);
    if(0 != bsp_rtc_wait_osci_stable(RCU_LXTAL))
	{
        return 0;
    }

    if(0 != *has_valid_backup)
	{
        rcu_osci_on(RCU_IRC32K);
        if(0 == bsp_rtc_wait_osci_stable(RCU_IRC32K))
		{
            rcu_periph_clock_enable(RCU_RTC);
            if(ERROR != rtc_register_sync_wait())
			{
                rtc_current_time_get(&saved_time);
                saved_time_valid = 1;
            }
        }
    }

    rcu_bkp_reset_enable();
    rcu_bkp_reset_disable();

    rcu_osci_on(RCU_LXTAL);
    if(0 != bsp_rtc_wait_osci_stable(RCU_LXTAL))
	{
        rtc_clock_ready = 0;
        *has_valid_backup = 0;
        return -1;
    }

    rcu_rtc_clock_config(RCU_RTCSRC_LXTAL);
    prescaler_s = 0xFFU;
    prescaler_a = 0x7FU;
    rcu_periph_clock_enable(RCU_RTC);

    if(0 != saved_time_valid)
	{
        saved_time.factor_asyn = prescaler_a;
        saved_time.factor_syn = prescaler_s;
        if(ERROR == rtc_init(&saved_time))
		{
            *has_valid_backup = 0;
            return -1;
        }

        RTC_BKP0 = BKP_VALUE;
        rtc_current_time_get(&rtc_initpara);
        *has_valid_backup = 1;
    }
	else
	{
        *has_valid_backup = 0;
    }

    rtcsrc_flag = GET_BITS(RCU_BDCTL, 8, 9);
    return 0;
}

/*
 * 配置RTC时钟源和分频；已有备份域时不强切RTCSRC。
 */
static int bsp_rtc_pre_cfg(uint8_t *has_valid_backup)
{
    int ret = -1;

    if(!has_valid_backup)
	{
        return -1;
    }

    rtcsrc_flag = GET_BITS(RCU_BDCTL, 8, 9);

#if defined(RTC_CLOCK_SOURCE_IRC32K)
    rcu_osci_on(RCU_IRC32K);
    ret = bsp_rtc_wait_osci_stable(RCU_IRC32K);
    if(0 != ret) {
        rtc_clock_ready = 0;
        return -1;
    }

    if(rtcsrc_flag == 0) {
        rcu_rtc_clock_config(RCU_RTCSRC_IRC32K);
    }

    prescaler_s = 0x13FU;
    prescaler_a = 0x63U;
#elif defined(RTC_CLOCK_SOURCE_LXTAL)
    if(0 != bsp_rtc_try_restore_lxtal_from_irc32k(has_valid_backup))
	{
        rtc_clock_ready = 0;
        return -1;
    }

    if(2 == rtcsrc_flag)
	{
        rcu_osci_on(RCU_IRC32K);
        ret = bsp_rtc_wait_osci_stable(RCU_IRC32K);
        if(0 != ret) {
            rtc_clock_ready = 0;
            return -1;
        }

        prescaler_s = 0x13FU;
        prescaler_a = 0x63U;
    }
	else
	{
        rcu_osci_on(RCU_LXTAL);
        ret = bsp_rtc_wait_osci_stable(RCU_LXTAL);
        if(0 == ret)
		{
            if(rtcsrc_flag == 0)
			{
                rcu_rtc_clock_config(RCU_RTCSRC_LXTAL);
            }

            prescaler_s = 0xFFU;
            prescaler_a = 0x7FU;
        }
		else
		{
#if RTC_CLOCK_FALLBACK_IRC32K_ENABLE
            if(rtcsrc_flag == 0)
			{
                rcu_osci_on(RCU_IRC32K);
                ret = bsp_rtc_wait_osci_stable(RCU_IRC32K);
                if(0 != ret)
				{
                    rtc_clock_ready = 0;
                    return -1;
                }

                rcu_rtc_clock_config(RCU_RTCSRC_IRC32K);
                prescaler_s = 0x13FU;
                prescaler_a = 0x63U;
            }
			else
			{
                rtc_clock_ready = 0;
                return -1;
            }
#else
            rtc_clock_ready = 0;
            return -1;
#endif
        }
    }
#else
#error RTC clock source should be defined.
#endif

    rcu_periph_clock_enable(RCU_RTC);
    rtc_clock_ready = 1;
    return 0;
}

/*
 * 初始化RTC：备份域有效则恢复，否则写默认时间。
 */
int bsp_rtc_init(void)
{
    int ret = 0;
    uint8_t has_valid_backup = 0;

    rcu_periph_clock_enable(RCU_PMU);
    pmu_backup_write_enable();

    has_valid_backup = bsp_rtc_has_valid_backup();

    if(0 != bsp_rtc_pre_cfg(&has_valid_backup))
	{
        rcu_all_reset_flag_clear();
        return -1;
    }

    if(0 != has_valid_backup)
	{
        ret = bsp_rtc_restore_from_backup();
    }
	else
	{
        ret = bsp_rtc_setup();
    }

    rcu_all_reset_flag_clear();
    return ret;
}

/*
 * 读取当前RTC日期时间。
 */
int bsp_rtc_get_datetime(bsp_rtc_datetime_t *datetime)
{
    if(!datetime)
	{
        return -1;
    }

    if(0 == rtc_clock_ready)
	{
        return -1;
    }

    if(ERROR == rtc_register_sync_wait())
	{
        return -1;
    }

    rtc_current_time_get(&rtc_initpara);

    datetime->year = (uint16_t)(2000 + bsp_rtc_bcd_to_decimal(rtc_initpara.year));
    datetime->month = bsp_rtc_bcd_to_decimal(rtc_initpara.month);
    datetime->date = bsp_rtc_bcd_to_decimal(rtc_initpara.date);
    datetime->hour = bsp_rtc_bcd_to_decimal(rtc_initpara.hour);
    datetime->minute = bsp_rtc_bcd_to_decimal(rtc_initpara.minute);
    datetime->second = bsp_rtc_bcd_to_decimal(rtc_initpara.second);
    datetime->day_of_week = (uint8_t)rtc_initpara.day_of_week;
    return 0;
}

/*
 * 设置RTC日期时间，成功后刷新rtc_initpara。
 */
int bsp_rtc_set_datetime(const bsp_rtc_datetime_t *datetime)
{
    rtc_parameter_struct new_time;
    uint8_t rtc_year;

    if(0 == bsp_rtc_is_valid_datetime(datetime))
	{
        return -1;
    }

    if(0 == rtc_clock_ready)
	{
        return -1;
    }
	
    rcu_periph_clock_enable(RCU_PMU);
    pmu_backup_write_enable();

    if(ERROR == rtc_register_sync_wait())
	{
        return -1;
    }

    rtc_current_time_get(&new_time);

    rtc_year = (uint8_t)(datetime->year - 2000);
    new_time.year = bsp_rtc_decimal_to_bcd(rtc_year);
    new_time.month = bsp_rtc_decimal_to_bcd(datetime->month);
    new_time.date = bsp_rtc_decimal_to_bcd(datetime->date);
    new_time.day_of_week = bsp_rtc_calculate_day_of_week(datetime->year, datetime->month, datetime->date);
    new_time.hour = bsp_rtc_decimal_to_bcd(datetime->hour);
    new_time.minute = bsp_rtc_decimal_to_bcd(datetime->minute);
    new_time.second = bsp_rtc_decimal_to_bcd(datetime->second);
    new_time.am_pm = RTC_AM;
    new_time.display_format = RTC_24HOUR;

    if(ERROR == rtc_init(&new_time))
	{
        return -1;
    }

    RTC_BKP0 = BKP_VALUE;
    rtc_current_time_get(&rtc_initpara);
    return 0;
}
