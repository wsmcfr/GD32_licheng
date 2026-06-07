#include "cimc_power_app.h"
#include "bsp_usart.h"
#include "cimc_params.h"
#include "led_app.h"
#include "scheduler.h"

/* RTC自动唤醒 */
#define SLEEP_SECONDS     10
#define WAKEUP_TIMER_VAL  ((uint16_t)(SLEEP_SECONDS - 1))

static const uint8_t s_wakeup_str[] = "instrument wakeup"; /* 唤醒后发送的ASCII字符串 */

/* 配置并使能RTC自动唤醒定时器 */
static void rtc_wakeup_start(void)
{
    rtc_wakeup_disable();
    while(RESET == rtc_flag_get(RTC_FLAG_WTW)) {}

    rtc_wakeup_clock_set(WAKEUP_CKSPRE);
    rtc_wakeup_timer_set(WAKEUP_TIMER_VAL);

    rtc_flag_clear(RTC_FLAG_WT);
    exti_flag_clear(EXTI_22);

    rtc_interrupt_enable(RTC_INT_WAKEUP);
    rtc_wakeup_enable();
}

/* 关闭RTC唤醒定时器并清理标志 */
static void rtc_wakeup_stop(void)
{
    rtc_wakeup_disable();
    rtc_interrupt_disable(RTC_INT_WAKEUP);
    rtc_flag_clear(RTC_FLAG_WT);
    exti_flag_clear(EXTI_22);
    nvic_irq_disable(RTC_WKUP_IRQn);
}

/*
 * 深度睡眠唤醒后手动恢复系统时钟至240MHz（HXTAL+PLL）。
 */
static void restore_clock(void)
{
    uint32_t reg_temp;

    RCU_CTL |= RCU_CTL_HXTALEN;
    while(RESET == (RCU_CTL & RCU_CTL_HXTALSTB)) {}

    rcu_periph_clock_enable(RCU_PMU);
    PMU_CTL |= PMU_CTL_LDOVS;

    RCU_CFG0 &= ~(RCU_CFG0_AHBPSC | RCU_CFG0_APB2PSC | RCU_CFG0_APB1PSC);
    RCU_CFG0 |= (RCU_AHB_CKSYS_DIV1 | RCU_APB2_CKAHB_DIV2 | RCU_APB1_CKAHB_DIV4);

    RCU_PLL = (25 | (480 << 6) | (((2 >> 1) - 1) << 16) | (RCU_PLLSRC_HXTAL) | (10 << 24));

    RCU_CTL |= RCU_CTL_PLLEN;
    while(RESET == (RCU_CTL & RCU_CTL_PLLSTB)) {}

    PMU_CTL |= PMU_CTL_HDEN;
    while(RESET == pmu_flag_get(PMU_FLAG_HDRF)) {}
    PMU_CTL |= PMU_CTL_HDS;
    while(RESET == pmu_flag_get(PMU_FLAG_HDSRF)) {}

    reg_temp = RCU_CFG0;
    reg_temp &= ~RCU_CFG0_SCS;
    reg_temp |= RCU_CKSYSSRC_PLLP;
    RCU_CFG0 = reg_temp;
    while(RESET == (RCU_CFG0 & RCU_SCSS_PLLP)) {}
}

/*执行深度睡眠10s流程*/
void power_sleep(void)
{
    led_app_blank_for_sleep();

    delay_ms(5);

    SysTick->CTRL = 0;

    rtc_wakeup_start();

    exti_init(EXTI_22, EXTI_INTERRUPT, EXTI_TRIG_RISING);
    nvic_irq_enable(RTC_WKUP_IRQn, 0, 0);

    rcu_periph_clock_enable(RCU_PMU);

    pmu_to_deepsleepmode(PMU_LDO_NORMAL, PMU_LOWDRIVER_DISABLE, WFI_CMD);

    rtc_wakeup_stop();
    restore_clock();

    systick_config();

    bsp_usart_init();
    {
        uint32_t saved_baud = params_baud();
        if(saved_baud != 19200) 
		{
            bsp_usart_change_baudrate(saved_baud);
        }
    }

    led_app_reset_cache();
    scheduler_reset_runtime();
    bsp_usart_send_buffer(RS485_USART, s_wakeup_str, (uint16_t)(sizeof(s_wakeup_str) - 1));
}
