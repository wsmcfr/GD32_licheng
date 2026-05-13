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
