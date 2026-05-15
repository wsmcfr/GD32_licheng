#include "bsp_rtc.h"

/* RTC 当前时间参数结构，供应用层读取显示。 */
rtc_parameter_struct rtc_initpara;

/* 以下变量只服务 RTC 初始化流程，因此限定在本文件内部。 */
static rtc_alarm_struct rtc_alarm;
static __IO uint32_t prescaler_a = 0U;
static __IO uint32_t prescaler_s = 0U;
static uint32_t rtcsrc_flag = 0U;

/*
 * 函数作用：
 *   将十进制数值转换成 GD32 RTC 期望的 BCD 编码格式。
 * 参数说明：
 *   value：十进制输入值，约定范围为 0~99。
 * 返回值说明：
 *   返回对应的 BCD 编码结果，高 4 位为十位，低 4 位为个位。
 */
static uint8_t bsp_rtc_decimal_to_bcd(uint8_t value)
{
    return (uint8_t)(((value / 10U) << 4) | (value % 10U));
}

/*
 * 函数作用：
 *   将 GD32 RTC 读出的 BCD 编码数值转换成十进制。
 * 参数说明：
 *   value：RTC 寄存器字段中的 BCD 编码值。
 * 返回值说明：
 *   返回转换后的十进制数值。
 */
static uint8_t bsp_rtc_bcd_to_decimal(uint8_t value)
{
    return (uint8_t)((((value >> 4) & 0x0FU) * 10U) + (value & 0x0FU));
}

/*
 * 函数作用：
 *   判断指定年份是否为公历闰年。
 * 参数说明：
 *   year：完整年份，例如 2026。
 * 返回值说明：
 *   1：表示闰年。
 *   0：表示平年。
 */
static uint8_t bsp_rtc_is_leap_year(uint16_t year)
{
    if((0U == (year % 400U)) || ((0U == (year % 4U)) && (0U != (year % 100U)))){
        return 1U;
    }

    return 0U;
}

/*
 * 函数作用：
 *   获取指定年月对应月份的最大天数。
 * 参数说明：
 *   year：完整年份，例如 2026。
 *   month：十进制月份，范围应为 1~12。
 * 返回值说明：
 *   28~31：表示该月最大天数。
 *   0：表示月份参数超出有效范围。
 */
static uint8_t bsp_rtc_get_days_in_month(uint16_t year, uint8_t month)
{
    static const uint8_t days_in_month[] = {31U, 28U, 31U, 30U, 31U, 30U, 31U, 31U, 30U, 31U, 30U, 31U};

    if((month < 1U) || (month > 12U)){
        return 0U;
    }

    if((2U == month) && (0U != bsp_rtc_is_leap_year(year))){
        return 29U;
    }

    return days_in_month[month - 1U];
}

/*
 * 函数作用：
 *   校验一组十进制年月日时分秒是否落在当前 RTC 接口允许的范围内。
 * 参数说明：
 *   datetime：待校验时间结构体指针，必须非空。
 * 返回值说明：
 *   1：表示所有字段都在允许范围内。
 *   0：表示存在空指针、非法年份、非法日期或非法时分秒。
 */
static uint8_t bsp_rtc_is_valid_datetime(const bsp_rtc_datetime_t *datetime)
{
    uint8_t max_day;

    if(NULL == datetime){
        return 0U;
    }

    if((datetime->year < 2000U) || (datetime->year > 2099U)){
        return 0U;
    }

    if((datetime->month < 1U) || (datetime->month > 12U)){
        return 0U;
    }

    max_day = bsp_rtc_get_days_in_month(datetime->year, datetime->month);
    if((0U == max_day) || (datetime->date < 1U) || (datetime->date > max_day)){
        return 0U;
    }

    if(datetime->hour > 23U){
        return 0U;
    }

    if((datetime->minute > 59U) || (datetime->second > 59U)){
        return 0U;
    }

    return 1U;
}

/*
 * 函数作用：
 *   根据公历年月日计算星期字段，供 RTC 寄存器写入时使用。
 * 参数说明：
 *   year：完整年份，例如 2026。
 *   month：十进制月份，范围为 1~12。
 *   date：十进制日期，范围为 1~31。
 * 返回值说明：
 *   `RTC_MONDAY`~`RTC_SUNDAY`：表示对应星期枚举。
 */
static uint8_t bsp_rtc_calculate_day_of_week(uint16_t year, uint8_t month, uint8_t date)
{
    static const uint8_t month_offset[] = {0U, 3U, 2U, 5U, 0U, 3U, 5U, 1U, 4U, 6U, 2U, 4U};
    uint16_t adjusted_year = year;
    uint8_t weekday_index;

    /*
     * 采用 Sakamoto 算法计算星期。
     * 对 1、2 月先视作上一年的第 13、14 月，以便统一闰年修正。
     */
    if(month < 3U){
        adjusted_year--;
    }

    weekday_index = (uint8_t)((adjusted_year +
                               (adjusted_year / 4U) -
                               (adjusted_year / 100U) +
                               (adjusted_year / 400U) +
                               month_offset[month - 1U] +
                               date) % 7U);

    if(0U == weekday_index){
        return RTC_SUNDAY;
    }

    return weekday_index;
}

/*
 * 函数作用：
 *   写入一组默认 RTC 时间，作为掉电后无备份域数据时的起始值。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   0：表示写入成功，并已更新备份寄存器标记。
 *  -1：表示 rtc_init 调用失败，默认时间未可靠写入。
 */
static int bsp_rtc_setup(void)
{
    int ret = 0;
    uint32_t tmp_hh = 0x23U;
    uint32_t tmp_mm = 0x59U;
    uint32_t tmp_ss = 0x50U;

    UNUSED_PARAM(rtc_alarm);

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

    if (ERROR == rtc_init(&rtc_initpara)) {
        ret = -1;
    } else {
        RTC_BKP0 = BKP_VALUE;
    }

    return ret;
}

/*
 * 函数作用：
 *   判断当前 RTC 备份域是否已经保存过有效时间上下文。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   1：表示 RTC_BKP0 中已写入本工程约定的有效标记，可认为 VBAT/备份域仍保持有效。
 *   0：表示未检测到有效标记，本次需要按首次初始化流程写入默认时间。
 */
static uint8_t bsp_rtc_has_valid_backup(void)
{
    if(RTC_BKP0 == BKP_VALUE) {
        return 1U;
    }

    return 0U;
}

/*
 * 函数作用：
 *   在 RTC 备份域仍然有效时，同步当前 RTC 寄存器到共享时间结构体。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   0：表示同步成功，当前时间已刷新到 rtc_initpara。
 *  -1：表示 RTC 阴影寄存器同步失败，当前时间不可可靠读取。
 */
static int bsp_rtc_restore_from_backup(void)
{
    if(ERROR == rtc_register_sync_wait()) {
        return -1;
    }

    rtc_current_time_get(&rtc_initpara);
    return 0;
}

/*
 * 函数作用：
 *   预配置 RTC 时钟源和同步分频参数。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   无返回值。
 * 说明：
 *   当前工程固定使用 LXTAL，因此这里只保留该路径。
 */
static void bsp_rtc_pre_cfg(void)
{
    /*
     * 若备份域中已经保留了 RTC 时钟源选择，就不要再次改写 RTCSRC。
     * 否则在主电掉电但 VBAT 仍供电的场景下，可能把正在运行的 RTC 重新切源，
     * 破坏“断电续时”的预期。
     */
    rtcsrc_flag = GET_BITS(RCU_BDCTL, 8U, 9U);

#if defined(RTC_CLOCK_SOURCE_IRC32K)
    rcu_osci_on(RCU_IRC32K);
    rcu_osci_stab_wait(RCU_IRC32K);
    if(rtcsrc_flag == 0U) {
        rcu_rtc_clock_config(RCU_RTCSRC_IRC32K);
    }

    prescaler_s = 0x13FU;
    prescaler_a = 0x63U;
#elif defined(RTC_CLOCK_SOURCE_LXTAL)
    rcu_osci_on(RCU_LXTAL);
    rcu_osci_stab_wait(RCU_LXTAL);
    if(rtcsrc_flag == 0U) {
        rcu_rtc_clock_config(RCU_RTCSRC_LXTAL);
    }

    prescaler_s = 0xFFU;
    prescaler_a = 0x7FU;
#else
#error RTC clock source should be defined.
#endif

    rcu_periph_clock_enable(RCU_RTC);
}

/*
 * 函数作用：
 *   初始化 RTC 外设；若备份域仍有效则直接恢复当前时间，否则写入默认时间。
 * 主要流程：
 *   1. 打开 PMU 备份域写权限。
 *   2. 配置或恢复 RTC 时钟源。
 *   3. 若备份域有效则读取当前时间；否则写入默认时间并写入备份标记。
 *   4. 清除所有复位标志位。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   0：表示初始化流程执行完成，且 RTC 当前时间可用。
 *  -1：表示首次建表写默认时间失败，或备份域恢复同步失败。
 */
int bsp_rtc_init(void)
{
    int ret = 0;
    uint8_t has_valid_backup = 0U;

    rcu_periph_clock_enable(RCU_PMU);
    pmu_backup_write_enable();

    /*
     * 先读取备份寄存器标记，再决定后续是“恢复现有 RTC”还是“首次建表”。
     * 该标记位保存在 RTC 备份域，主电掉电但 VBAT 仍在时会继续保持。
     */
    has_valid_backup = bsp_rtc_has_valid_backup();

    bsp_rtc_pre_cfg();

    if(0U != has_valid_backup) {
        /* 备份域有效时只同步当前时间，不能再重写默认时间。 */
        ret = bsp_rtc_restore_from_backup();
    } else {
        /* 无备份域时按冷启动流程写入默认时间，并建立备份域有效标记。 */
        ret = bsp_rtc_setup();
    }

    rcu_all_reset_flag_clear();
    return ret;
}

/*
 * 函数作用：
 *   读取当前 RTC 的完整年月日时分秒，并输出为十进制时间结构体。
 * 主要流程：
 *   1. 先等待 RTC 阴影寄存器同步，避免读到跨秒更新中的不一致字段。
 *   2. 把当前寄存器内容刷新到共享结构 `rtc_initpara`。
 *   3. 将 RTC 的 BCD 字段统一转换成串口命令层更容易处理的十进制格式。
 * 参数说明：
 *   datetime：输出时间结构体指针，必须非空。
 * 返回值说明：
 *   0：表示读取成功。
 *  -1：表示参数无效，或 RTC 阴影寄存器同步失败。
 */
int bsp_rtc_get_datetime(bsp_rtc_datetime_t *datetime)
{
    if(NULL == datetime){
        return -1;
    }

    if(ERROR == rtc_register_sync_wait()) {
        return -1;
    }

    rtc_current_time_get(&rtc_initpara);

    datetime->year = (uint16_t)(2000U + bsp_rtc_bcd_to_decimal(rtc_initpara.year));
    datetime->month = bsp_rtc_bcd_to_decimal(rtc_initpara.month);
    datetime->date = bsp_rtc_bcd_to_decimal(rtc_initpara.date);
    datetime->hour = bsp_rtc_bcd_to_decimal(rtc_initpara.hour);
    datetime->minute = bsp_rtc_bcd_to_decimal(rtc_initpara.minute);
    datetime->second = bsp_rtc_bcd_to_decimal(rtc_initpara.second);
    datetime->day_of_week = (uint8_t)rtc_initpara.day_of_week;
    return 0;
}

/*
 * 函数作用：
 *   按十进制年月日时分秒设置 RTC，并自动生成星期字段后写入寄存器。
 * 主要流程：
 *   1. 在驱动层再次校验时间范围，防止上层绕过串口解析直接传入非法值。
 *   2. 读取当前 RTC 分频配置，避免改时间时误改时钟基准参数。
 *   3. 把十进制输入转换成 RTC 所需的 BCD 字段，调用 rtc_init() 完成写入。
 *   4. 写回备份域有效标记，并刷新共享时间结构供显示任务后续直接复用。
 * 参数说明：
 *   datetime：待写入的十进制时间结构体指针，必须非空。
 * 返回值说明：
 *   0：表示设置成功。
 *  -1：表示参数无效、RTC 同步失败，或底层 rtc_init() 写入失败。
 */
int bsp_rtc_set_datetime(const bsp_rtc_datetime_t *datetime)
{
    rtc_parameter_struct new_time;
    uint8_t rtc_year;

    if(0U == bsp_rtc_is_valid_datetime(datetime)){
        return -1;
    }

    /*
     * 设置 RTC 寄存器前确保备份域仍允许写入。
     * 这样即便后续有模块单独调用本接口，也不会依赖启动阶段的隐式状态。
     */
    rcu_periph_clock_enable(RCU_PMU);
    pmu_backup_write_enable();

    if(ERROR == rtc_register_sync_wait()) {
        return -1;
    }

    rtc_current_time_get(&new_time);

    rtc_year = (uint8_t)(datetime->year - 2000U);
    new_time.year = bsp_rtc_decimal_to_bcd(rtc_year);
    new_time.month = bsp_rtc_decimal_to_bcd(datetime->month);
    new_time.date = bsp_rtc_decimal_to_bcd(datetime->date);
    new_time.day_of_week = bsp_rtc_calculate_day_of_week(datetime->year, datetime->month, datetime->date);
    new_time.hour = bsp_rtc_decimal_to_bcd(datetime->hour);
    new_time.minute = bsp_rtc_decimal_to_bcd(datetime->minute);
    new_time.second = bsp_rtc_decimal_to_bcd(datetime->second);
    new_time.am_pm = RTC_AM;
    new_time.display_format = RTC_24HOUR;

    if(ERROR == rtc_init(&new_time)) {
        return -1;
    }

    RTC_BKP0 = BKP_VALUE;
    rtc_current_time_get(&rtc_initpara);
    return 0;
}
