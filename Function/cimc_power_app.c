#include "cimc_power_app.h"
#include "bsp_usart.h"
#include "cimc_params.h"
#include "led_app.h"
#include "scheduler.h"

/* RTC自动唤醒：ck_spre（1Hz），计数值9→10s后唤醒 */
#define SLEEP_SECONDS     10
#define WAKEUP_TIMER_VAL  ((uint16_t)(SLEEP_SECONDS - 1))

static const uint8_t s_wakeup_str[] = "instrument wakeup"; /* 唤醒后发送的ASCII字符串 */

/* 配置并使能RTC自动唤醒定时器（ck_spre，10s后触发EXTI_22） */
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

    /* 使能HXTAL，无超时等待稳定（深度睡眠后1~3ms必然稳定） */
    RCU_CTL |= RCU_CTL_HXTALEN;
    while(RESET == (RCU_CTL & RCU_CTL_HXTALSTB)) {}

    rcu_periph_clock_enable(RCU_PMU);
    PMU_CTL |= PMU_CTL_LDOVS;

    /* AHB=SYSCLK，APB2=AHB/2，APB1=AHB/4 */
    RCU_CFG0 &= ~(RCU_CFG0_AHBPSC | RCU_CFG0_APB2PSC | RCU_CFG0_APB1PSC);
    RCU_CFG0 |= (RCU_AHB_CKSYS_DIV1 | RCU_APB2_CKAHB_DIV2 | RCU_APB1_CKAHB_DIV4);

    /* 主PLL：25MHz HXTAL，M=25，N=480，P=2 → 240MHz */
    RCU_PLL = (25 | (480 << 6) | (((2 >> 1) - 1) << 16) | (RCU_PLLSRC_HXTAL) | (10 << 24));

    RCU_CTL |= RCU_CTL_PLLEN;
    while(RESET == (RCU_CTL & RCU_CTL_PLLSTB)) {}

    /* High-Drive模式，240MHz必需，分两步使能 */
    PMU_CTL |= PMU_CTL_HDEN;
    while(RESET == pmu_flag_get(PMU_FLAG_HDRF)) {}
    PMU_CTL |= PMU_CTL_HDS;
    while(RESET == pmu_flag_get(PMU_FLAG_HDSRF)) {}

    /* 切换系统时钟到PLL_P，等待SCSS确认 */
    reg_temp = RCU_CFG0;
    reg_temp &= ~RCU_CFG0_SCS;
    reg_temp |= RCU_CKSYSSRC_PLLP;
    RCU_CFG0 = reg_temp;
    while(RESET == (RCU_CFG0 & RCU_SCSS_PLLP)) {}
}

/*
 * 执行深度睡眠10s流程：关外设→配置RTC唤醒→WFI→唤醒恢复→发"instrument wakeup"。
 * 收到0x03AA命令后由协议层调用（OK帧已在调用前发出）。
 */
void power_sleep(void)
{
    led_app_blank_for_sleep(); /* 进入睡眠前关闭LED */

    delay_ms(5); /* 等RS485当前帧完全移出 */

    SysTick->CTRL = 0; /* 停止SysTick，减少无效唤醒 */

    rtc_wakeup_start();

    /* EXTI_22（RTC唤醒）配置为上升沿中断，PMU深度睡眠只能被EXTI唤醒 */
    exti_init(EXTI_22, EXTI_INTERRUPT, EXTI_TRIG_RISING);
    nvic_irq_enable(RTC_WKUP_IRQn, 0, 0);

    rcu_periph_clock_enable(RCU_PMU);

    pmu_to_deepsleepmode(PMU_LDO_NORMAL, PMU_LOWDRIVER_DISABLE, WFI_CMD);

    /* ── 唤醒后恢复 ── */

    rtc_wakeup_stop();
    restore_clock();

    systick_config(); /* 恢复SysTick时基 */

    /* 重新初始化USART1，使用Flash保存的波特率 */
    bsp_usart_init();
    {
        uint32_t saved_baud = params_baud();
        if(saved_baud != 19200) 
		{
            bsp_usart_change_baudrate(saved_baud);
        }
    }

    led_app_reset_cache();
    scheduler_reset_runtime(); /* 避免各任务唤醒后全部同时到期 */

    bsp_usart_send_buffer(RS485_USART, s_wakeup_str, (uint16_t)(sizeof(s_wakeup_str) - 1));
}
