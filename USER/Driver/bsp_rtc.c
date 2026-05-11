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
 *   写入一组默认 RTC 时间，作为掉电后无备份域数据时的起始值。
 * 返回值说明：
 *   0  表示写入成功。
 *  -1  表示 rtc_init 调用失败。
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
 *   预配置 RTC 时钟源和同步分频参数。
 * 说明：
 *   当前工程固定使用 LXTAL，因此这里只保留该路径。
 */
static void bsp_rtc_pre_cfg(void)
{
#if defined(RTC_CLOCK_SOURCE_IRC32K)
    rcu_osci_on(RCU_IRC32K);
    rcu_osci_stab_wait(RCU_IRC32K);
    rcu_rtc_clock_config(RCU_RTCSRC_IRC32K);

    prescaler_s = 0x13FU;
    prescaler_a = 0x63U;
#elif defined(RTC_CLOCK_SOURCE_LXTAL)
    rcu_osci_on(RCU_LXTAL);
    rcu_osci_stab_wait(RCU_LXTAL);
    rcu_rtc_clock_config(RCU_RTCSRC_LXTAL);

    prescaler_s = 0xFFU;
    prescaler_a = 0x7FU;
#else
#error RTC clock source should be defined.
#endif

    rcu_periph_clock_enable(RCU_RTC);
    rtc_register_sync_wait();
}

/*
 * 函数作用：
 *   初始化 RTC 外设，并在无可靠备份域时强制写入默认时间。
 * 主要流程：
 *   1. 打开 PMU 备份域写权限。
 *   2. 配置 RTC 时钟源。
 *   3. 写入默认时间。
 *   4. 清除所有复位标志位。
 */
int bsp_rtc_init(void)
{
    int ret = 0;

    rcu_periph_clock_enable(RCU_PMU);
    pmu_backup_write_enable();

    bsp_rtc_pre_cfg();
    rtcsrc_flag = GET_BITS(RCU_BDCTL, 8U, 9U);
    UNUSED_PARAM(rtcsrc_flag);

    ret = bsp_rtc_setup();

    rcu_all_reset_flag_clear();
    return ret;
}
