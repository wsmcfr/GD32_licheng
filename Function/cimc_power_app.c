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
 */
static void prv_cimc_power_setup_rtc_wakeup(void)
{
    rtc_wakeup_disable();
    while(RESET == rtc_flag_get(RTC_FLAG_WTW)) {}

    rtc_wakeup_clock_set(WAKEUP_CKSPRE);
    rtc_wakeup_timer_set(CIMC_POWER_WAKEUP_TIMER_VAL);

    rtc_flag_clear(RTC_FLAG_WT);
    exti_flag_clear(EXTI_22);

    rtc_interrupt_enable(RTC_INT_WAKEUP);
    rtc_wakeup_enable();
}

/*
 * 函数作用：
 *   关闭 RTC 唤醒定时器并清理唤醒标志。
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
 *   深度睡眠唤醒后恢复系统时钟到 240MHz（HXTAL + PLL）。
 *
 * 为什么不直接调用 SystemInit()：
 *   SystemInit() 内部先将 HXTAL 禁用（RCU_CTL &= ~HXTALEN），再重新启用。
 *   深度睡眠期间 HXTAL 被硬件自动停止，重启后晶振需要 1~3ms 才能稳定；
 *   而 SystemInit() 使用的 HXTAL_STARTUP_TIMEOUT 循环上限约 0.5ms，
 *   超时后进入 while(0==HXTALSTB){} 无限循环，导致系统永久卡死。
 *
 * 本函数使用无超时限制的等待，晶振必然稳定后再继续，与 system_clock_240m_25m_hxtal()
 * 配置参数完全一致（PSC=25，N=480，P=2 → 240MHz）。
 */
static void prv_cimc_power_restore_clock(void)
{
    uint32_t reg_temp;

    /*
     * 使能 HXTAL 并无限等待稳定。
     * 深度睡眠期间 HXTAL（HSE）被硬件停止；重新使能后晶振在 1~3ms 内必然
     * 稳定，不会真正无限循环——只是等待时间比 HXTAL_STARTUP_TIMEOUT 稍长。
     */
    RCU_CTL |= RCU_CTL_HXTALEN;
    while(RESET == (RCU_CTL & RCU_CTL_HXTALSTB)) {}

    /* 确保 PMU APB1 时钟已使能，恢复 LDO 高压输出（240MHz 必需）。 */
    rcu_periph_clock_enable(RCU_PMU);
    PMU_CTL |= PMU_CTL_LDOVS;

    /* 配置总线分频：AHB=SYSCLK，APB2=AHB/2，APB1=AHB/4。 */
    RCU_CFG0 &= ~(RCU_CFG0_AHBPSC | RCU_CFG0_APB2PSC | RCU_CFG0_APB1PSC);
    RCU_CFG0 |= (RCU_AHB_CKSYS_DIV1 | RCU_APB2_CKAHB_DIV2 | RCU_APB1_CKAHB_DIV4);

    /*
     * 配置主 PLL：25MHz HXTAL，M=25，N=480，P=2 → VCO=480MHz → 240MHz。
     * 参数与 system_clock_240m_25m_hxtal() 完全一致。
     */
    RCU_PLL = (25U | (480U << 6U) | (((2U >> 1U) - 1U) << 16U) |
               (RCU_PLLSRC_HXTAL) | (10U << 24U));

    /* 使能 PLL 并等待锁定。 */
    RCU_CTL |= RCU_CTL_PLLEN;
    while(RESET == (RCU_CTL & RCU_CTL_PLLSTB)) {}

    /* 使能 High-Drive 模式，240MHz 运行必需，两步使能中间各自等待就绪。 */
    PMU_CTL |= PMU_CTL_HDEN;
    while(RESET == pmu_flag_get(PMU_FLAG_HDRF)) {}
    PMU_CTL |= PMU_CTL_HDS;
    while(RESET == pmu_flag_get(PMU_FLAG_HDSRF)) {}

    /* 切换系统时钟到 PLL_P 并等待硬件确认（SCSS = 10）。 */
    reg_temp = RCU_CFG0;
    reg_temp &= ~RCU_CFG0_SCS;
    reg_temp |= RCU_CKSYSSRC_PLLP;
    RCU_CFG0 = reg_temp;
    while(RESET == (RCU_CFG0 & RCU_SCSS_PLLP)) {}
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
    /* 关闭 LED，避免 LED 电流在评审电流表上产生干扰。 */
    led_app_blank_for_sleep();

    /* 等待当前 RS485 发送帧完全移出（OK 帧在调用本函数前已由协议层发出）。 */
    delay_ms(5U);

    /* 停止 SysTick 中断，减少睡眠期间的无效唤醒干扰。 */
    SysTick->CTRL = 0U;

    /* 配置 RTC 自动唤醒定时器为 10 秒，并使能 EXTI_22 唤醒线。 */
    prv_cimc_power_setup_rtc_wakeup();

    /*
     * 配置 EXTI_22（RTC 唤醒）为上升沿中断模式。
     * PMU 深度睡眠只能被 EXTI 中断唤醒，RTC 唤醒事件通过 EXTI_22 路由。
     */
    exti_init(EXTI_22, EXTI_INTERRUPT, EXTI_TRIG_RISING);
    nvic_irq_enable(RTC_WKUP_IRQn, 0U, 0U);

    rcu_periph_clock_enable(RCU_PMU);

    /* ── 进入深度睡眠，WFI 等待 RTC 唤醒事件 ── */
    pmu_to_deepsleepmode(PMU_LDO_NORMAL, PMU_LOWDRIVER_DISABLE, WFI_CMD);

    /* ── 唤醒后恢复 ── */

    /* 关闭唤醒定时器，清标志，防止重复触发。 */
    prv_cimc_power_teardown_rtc_wakeup();

    /*
     * 恢复系统时钟到 240MHz（HXTAL + PLL）。
     * 不调用 SystemInit()：深度睡眠后 HXTAL 停止，SystemInit() 内部等待 HXTAL 稳定
     * 时使用 HXTAL_STARTUP_TIMEOUT（~0.5ms），而晶振重启需 1~3ms，导致超时后
     * 进入无限循环卡死。自定义函数使用无超时等待，晶振必然稳定。
     */
    prv_cimc_power_restore_clock();

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
