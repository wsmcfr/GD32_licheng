#include "cimc_power_app.h"
#include "bsp_usart.h"
#include "cimc_params.h"
#include "led_app.h"
#include "scheduler.h"

/*
 * RTC 自动唤醒定时器使用 ck_spre（1 Hz）时钟源。
 * 计数值 = 9 → 经过 10 次降沿 → 10 秒后触发唤醒。
 */
#define CIMC_POWER_SLEEP_SECONDS      10U
#define CIMC_POWER_WAKEUP_TIMER_VAL   ((uint16_t)(CIMC_POWER_SLEEP_SECONDS - 1U))

/* 唤醒后向上位机发送的字符串，不经帧封装，直接 ASCII 输出。 */
static const uint8_t s_wakeup_str[] = "instrument wakeup";

/*
 * 函数作用：
 *   配置 RTC 自动唤醒定时器并使能。
 *   使用 ck_spre（1Hz）作为计数时钟，到期后在 EXTI_22 产生唤醒事件。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   无返回值。
 */
static void prv_cimc_power_setup_rtc_wakeup(void)
{
    /* 禁用唤醒定时器，等待寄存器可写标志（WTWF）置位。 */
    rtc_wakeup_disable();
    while(RESET == rtc_flag_get(RTC_FLAG_WTW)) {}

    /* 选择 ck_spre（1Hz）作为唤醒计数时钟，计数值写入唤醒定时器。 */
    rtc_wakeup_clock_set(WAKEUP_CKSPRE);
    rtc_wakeup_timer_set(CIMC_POWER_WAKEUP_TIMER_VAL);

    /* 清除可能残留的旧唤醒标志，避免上电就立即触发。 */
    rtc_flag_clear(RTC_FLAG_WT);
    exti_flag_clear(EXTI_22);

    /* 使能 RTC 唤醒中断并启动定时器。 */
    rtc_interrupt_enable(RTC_INT_WAKEUP);
    rtc_wakeup_enable();
}

/*
 * 函数作用：
 *   关闭 RTC 唤醒定时器并清理唤醒标志。
 *   在唤醒后调用，防止定时器持续触发中断。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   无返回值。
 */
static void prv_cimc_power_teardown_rtc_wakeup(void)
{
    rtc_wakeup_disable();
    rtc_interrupt_disable(RTC_INT_WAKEUP);
    rtc_flag_clear(RTC_FLAG_WT);
    exti_flag_clear(EXTI_22);

    nvic_irq_disable(RTC_WKUP_IRQn);
}

/*
 * 函数作用：
 *   执行赛题 J-01 睡眠流程：关外设 → 配置 RTC 唤醒 → 深度睡眠 → 唤醒恢复 → 发字符串。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   无返回值；函数在唤醒并完成恢复后正常返回。
 */
void cimc_power_sleep_10s(void)
{
    /* ── 进入睡眠前准备 ── */

    /* 关闭 LED，避免 LED 电流在评审电流表上产生干扰。 */
    led_app_blank_for_sleep();

    /* 等待当前 RS485 发送帧完全移出（OK 帧在调用本函数前已由协议层发出）。 */
    delay_ms(5U);

    /* 停止 SysTick 中断，减少睡眠期间的唤醒干扰。 */
    SysTick->CTRL = 0U;

    /* 配置 RTC 自动唤醒定时器为 10 秒，并使能 EXTI_22 唤醒线。 */
    prv_cimc_power_setup_rtc_wakeup();

    /*
     * 配置 EXTI_22（RTC 唤醒）为上升沿中断模式。
     * PMU 深度睡眠只能被 EXTI 中断唤醒，RTC 唤醒事件通过 EXTI_22 路由。
     */
    exti_init(EXTI_22, EXTI_INTERRUPT, EXTI_TRIG_RISING);
    nvic_irq_enable(RTC_WKUP_IRQn, 0U, 0U);

    /* 允许从深度睡眠中断返回时自动清除 SLEEPDEEP 标志。 */
    rcu_periph_clock_enable(RCU_PMU);

    /* ── 进入深度睡眠，WFI 等待 RTC 唤醒事件 ── */
    pmu_to_deepsleepmode(PMU_LDO_NORMAL, PMU_LOWDRIVER_DISABLE, WFI_CMD);

    /* ── 唤醒后恢复 ── */

    /* 关闭唤醒定时器，清标志，防止重复触发。 */
    prv_cimc_power_teardown_rtc_wakeup();

    /*
     * 深度睡眠退出后系统时钟回落到 IRC16M（16MHz HSI）。
     * 调用 SystemInit() 重新配置 HXTAL 和 PLL，恢复 240MHz 工作频率。
     */
    SystemInit();

    /* 恢复 SysTick 以提供毫秒时基，scheduler_run() 依赖它。 */
    systick_config();

    /* 重新初始化 USART1/RS485，使用 Flash 中保存的波特率。 */
    bsp_usart_init();
    {
        uint32_t saved_baud = cimc_params_get_baud_rate();
        if(saved_baud != 19200U) {
            bsp_usart_change_baudrate(saved_baud);
        }
    }

    /* 恢复 LED 缓存和调度器时基，避免各任务在下一轮全部同时到期。 */
    led_app_reset_cache();
    scheduler_reset_runtime();

    /*
     * 赛题规定唤醒后直接回复 ASCII 字符串，不经帧封装。
     * 字符串内容固定为 "instrument wakeup"。
     */
    (void)bsp_usart_send_buffer(RS485_USART,
                                s_wakeup_str,
                                (uint16_t)(sizeof(s_wakeup_str) - 1U));
}
