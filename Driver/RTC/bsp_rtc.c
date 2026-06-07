#include "bsp_rtc.h"

/* RTC 当前时间参数结构，供应用层读取显示。 */
rtc_parameter_struct rtc_initpara;

/* 以下变量只服务 RTC 初始化流程，因此限定在本文件内部。 */
static rtc_alarm_struct rtc_alarm;
static __IO uint32_t prescaler_a = 0;
static __IO uint32_t prescaler_s = 0;
static uint32_t rtcsrc_flag = 0;
static int rtc_clock_ready = 0;
static uint8_t rtc_lxtal_recovered = 0;

// 从 RCU_BDCTL 的 RTCSRC 位解码 RTC 当前时钟源，返回 bsp_rtc_clock_source_t 枚举。
static bsp_rtc_clock_source_t bsp_rtc_decode_clock_source(uint32_t bdctl)
{
    return (bsp_rtc_clock_source_t)GET_BITS(bdctl, 8, 9);
}

// 将十进制数值转换成 GD32 RTC 期望的 BCD 编码格式，高 4 位为十位，低 4 位为个位。
static uint8_t bsp_rtc_decimal_to_bcd(uint8_t value)
{
    return (uint8_t)(((value / 10) << 4) | (value % 10));
}

// 将 GD32 RTC 读出的 BCD 编码数值转换成十进制。
static uint8_t bsp_rtc_bcd_to_decimal(uint8_t value)
{
    return (uint8_t)((((value >> 4) & 0x0FU) * 10) + (value & 0x0FU));
}

// 判断指定年份是否为公历闰年，返回 1 表示闰年，0 表示平年。
static uint8_t bsp_rtc_is_leap_year(uint16_t year)
{
    if((0 == (year % 400)) || ((0 == (year % 4)) && (0 != (year % 100))))
	{
        return 1;
    }

    return 0;
}

// 获取指定年月对应的最大天数，月份超出 1~12 时返回 0。
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

// 等待 RCU 振荡器稳定，库自带超时，失败时向上返回 -1。
static int bsp_rtc_wait_osci_stable(rcu_osci_type_enum osci)
{
    if(SUCCESS == rcu_osci_stab_wait(osci)) 
	{
        return 0;
    }

    return -1;
}

// 校验十进制年月日时分秒是否都在合法范围内，空指针或非法字段均返回 0。
static uint8_t bsp_rtc_is_valid_datetime(const bsp_rtc_datetime_t *datetime)
{
    uint8_t max_day;

    if(NULL == datetime)
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

// 用 Sakamoto 算法根据公历年月日计算星期，返回 RTC_MONDAY~RTC_SUNDAY。
static uint8_t bsp_rtc_calculate_day_of_week(uint16_t year, uint8_t month, uint8_t date)
{
    static const uint8_t month_offset[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
    uint16_t adjusted_year = year;
    uint8_t weekday_index;

    /*
     * 采用 Sakamoto 算法计算星期。
     * 对 1、2 月先视作上一年的第 13、14 月，以便统一闰年修正。
     */
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

/*
 * 备份域无效时写入默认时间作为冷启动起始值；成功后写入 BKP_VALUE 标记备份域。
 * 所有分频参数从 prescaler_a / prescaler_s 全局变量读取，避免重复硬编码。
 */
static int bsp_rtc_setup(void)
{
    int ret = 0;
    uint32_t tmp_hh = 0x23U;
    uint32_t tmp_mm = 0x59U;
    uint32_t tmp_ss = 0x50U;

    /* rtc_alarm 目前仅保留为本模块的静态占位状态，初始化默认表时无需改写。 */
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

// 检查 RTC_BKP0 是否已写入本工程约定的有效标记，主电掉电但 VBAT 仍在时该标记保持。
static uint8_t bsp_rtc_has_valid_backup(void)
{
    if(RTC_BKP0 == BKP_VALUE) 
	{
        return 1;
    }

    return 0;
}

// 备份域有效时同步 RTC 阴影寄存器到 rtc_initpara，阴影同步失败返回 -1。
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
 * 若备份域中 RTCSRC 已经 fallback 到 IRC32K，则尝试自动迁回 LXTAL。
 * 仅在 LXTAL 当前已可稳定后才执行备份域复位；复位前先读出 IRC32K RTC 的时间快照，
 * 切源成功后以 LXTAL 分频参数写回快照，尽量保留 VBAT 保存的时间上下文。
 * 若 LXTAL 仍不可用则保守地继续沿用 IRC32K，避免破坏现有 RTC 计时。
 */
static int bsp_rtc_try_restore_lxtal_from_irc32k(uint8_t *has_valid_backup)
{
    rtc_parameter_struct saved_time;
    uint8_t saved_time_valid = 0;

    if(NULL == has_valid_backup) 
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

    /*
     * RTCSRC 位属于备份域。要从 IRC32K 切回 LXTAL，必须复位备份域清掉旧选择；
     * 只有在 LXTAL 已经稳定后才执行这一步，避免外部晶振异常时无意义丢失 RTC。
     */
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
        /*
         * 复位备份域会清掉 RTC_PSC，因此写回快照前必须把分频改成 LXTAL 对应参数。
         * 日期时间字段保持从旧 RTC 读出的 BCD 值，尽量保留现场已经走到的时间。
         */
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
    rtc_lxtal_recovered = 1;
    return 0;
}

/*
 * 预配置 RTC 时钟源并等待振荡器稳定，同时设置全局分频参数 prescaler_s / prescaler_a。
 * 若备份域已选过 RTCSRC，只等待对应振荡器稳定，不强制改写时钟源，以免在主电掉电
 * 但 VBAT 仍供电时破坏"断电续时"预期；冷启动首选 LXTAL 失败时视编译配置决定是否
 * fallback 到 IRC32K，已有 RTCSRC 时不允许 fallback（强切需复位备份域）。
 */
static int bsp_rtc_pre_cfg(uint8_t *has_valid_backup)
{
    int ret = -1;

    if(NULL == has_valid_backup) 
	{
        return -1;
    }

    /*
     * 若备份域中已经保留了 RTC 时钟源选择，就不要再次改写 RTCSRC。
     * 否则在主电掉电但 VBAT 仍供电的场景下，可能把正在运行的 RTC 重新切源，
     * 破坏"断电续时"的预期。
     */
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
        /*
         * 备份域显示 RTC 当前使用 IRC32K，说明之前可能已经从 LXTAL fallback。
         * 这种情况下继续等待 IRC32K，不能再按编译期首选 LXTAL 强行重选时钟源。
         */
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
                /*
                 * 只有冷启动且备份域尚未选择 RTC 时钟源时才允许切 IRC32K。
                 * 若已有 RTCSRC，强行切源需要复位备份域，会破坏 VBAT 保存的时间。
                 */
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
 * 初始化 RTC 外设：打开备份域写权限，配置时钟源，备份域有效时恢复现有时间，
 * 否则写入默认时间并建立备份域标记，最后清除所有复位标志位。
 */
int bsp_rtc_init(void)
{
    int ret = 0;
    uint8_t has_valid_backup = 0;

    rcu_periph_clock_enable(RCU_PMU);
    pmu_backup_write_enable();

    /*
     * 先读取备份寄存器标记，再决定后续是"恢复现有 RTC"还是"首次建表"。
     * 该标记位保存在 RTC 备份域，主电掉电但 VBAT 仍在时会继续保持。
     */
    has_valid_backup = bsp_rtc_has_valid_backup();

    rtc_lxtal_recovered = 0;

    if(0 != bsp_rtc_pre_cfg(&has_valid_backup)) 
	{
        rcu_all_reset_flag_clear();
        return -1;
    }

    if(0 != has_valid_backup) 
	{
        /* 备份域有效时只同步当前时间，不能再重写默认时间。 */
        ret = bsp_rtc_restore_from_backup();
    } 
	else 
	{
        /* 无备份域时按冷启动流程写入默认时间，并建立备份域有效标记。 */
        ret = bsp_rtc_setup();
    }

    rcu_all_reset_flag_clear();
    return ret;
}

/*
 * 读取当前 RTC 完整年月日时分秒，先等待阴影寄存器同步避免跨秒读取不一致，
 * 再将寄存器 BCD 字段统一转换成十进制写入输出结构体。
 */
int bsp_rtc_get_datetime(bsp_rtc_datetime_t *datetime)
{
    if(NULL == datetime)
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
 * 把当前 RTC 时间转为 2000-01-01 起算的秒级计数，仅用于深睡前后 RTC 秒差计算，
 * 不与 Unix 1970 epoch 对齐。
 */
int bsp_rtc_get_epoch_seconds(uint32_t *epoch_seconds)
{
    bsp_rtc_datetime_t datetime;
    uint16_t year;
    uint8_t month;
    uint32_t days;

    if(NULL == epoch_seconds) 
	{
        return -1;
    }

    if(0 != bsp_rtc_get_datetime(&datetime)) 
	{
        return -1;
    }

    if(0 == bsp_rtc_is_valid_datetime(&datetime)) 
	{
        return -1;
    }

    days = 0;
    for(year = 2000; year < datetime.year; year++) 
	{
        days += (0 != bsp_rtc_is_leap_year(year)) ? 366 : 365;
    }

    for(month = 1; month < datetime.month; month++) 
	{
        days += bsp_rtc_get_days_in_month(datetime.year, month);
    }

    days += (uint32_t)(datetime.date - 1);
    *epoch_seconds = (days * 86400) + ((uint32_t)datetime.hour * 3600) + ((uint32_t)datetime.minute * 60) + (uint32_t)datetime.second;
    return 0;
}

// 读取 RTC 当前诊断状态，包含时钟源、备份域标记、分频寄存器和校准寄存器快照。
int bsp_rtc_get_status(bsp_rtc_status_t *status)
{
    uint32_t bdctl;
    uint32_t psc;

    if(NULL == status) 
	{
        return -1;
    }

    bdctl = RCU_BDCTL;
    psc = RTC_PSC;

    status->clock_ready = (0 != rtc_clock_ready) ? 1 : 0;
    status->backup_valid = bsp_rtc_has_valid_backup();
    status->lxtal_recovered = rtc_lxtal_recovered;
    status->clock_source = bsp_rtc_decode_clock_source(bdctl);
    status->prescaler_a = (uint16_t)GET_BITS(psc, 16, 22);
    status->prescaler_s = (uint16_t)GET_BITS(psc, 0, 14);
    status->bdctl = bdctl;
    status->hrfc = RTC_HRFC;
    status->cosc = RTC_COSC;
    return 0;
}

/*
 * 按十进制年月日时分秒设置 RTC，自动推算星期字段后写入寄存器。
 * 驱动层在写入前重新校验时间范围，防止上层绕过串口解析直接传入非法值；
 * 写入前先同步阴影寄存器、读出当前分频，避免改时间时误改时钟基准；
 * 成功后写回 BKP_VALUE 并刷新 rtc_initpara 供显示任务直接复用。
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

    /*
     * 设置 RTC 寄存器前确保备份域仍允许写入。
     * 这样即便后续有模块单独调用本接口，也不会依赖启动阶段的隐式状态。
     */
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
