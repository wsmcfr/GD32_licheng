#include "bsp_key.h"

/*
 * 函数作用：
 *   初始化全部按键 GPIO。
 * 主要流程：
 *   1. 打开 GPIOA/GPIOB/GPIOC 时钟。
 *   2. 将 6 个普通按键和 1 个唤醒键分别配置为上拉输入。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   无返回值。
 * 说明：
 *   使用上拉输入是因为当前按键硬件接法按下时会拉低电平。
 */
void bsp_btn_init(void)
{
    rcu_periph_clock_enable(KEYB_CLK_PORT);
    rcu_periph_clock_enable(KEYC_CLK_PORT);
    rcu_periph_clock_enable(KEYA_CLK_PORT);

    gpio_mode_set(KEYB_PORT, GPIO_MODE_INPUT, GPIO_PUPD_PULLUP, KEY1_PIN);
    gpio_mode_set(KEYC_PORT, GPIO_MODE_INPUT, GPIO_PUPD_PULLUP, KEY2_PIN | KEY3_PIN);
    gpio_mode_set(KEYA_PORT, GPIO_MODE_INPUT, GPIO_PUPD_PULLUP, KEY4_PIN | KEY5_PIN | KEY6_PIN | KEYW_PIN);
}

/*
 * 函数作用：
 *   配置唤醒按键对应的 EXTI0 外部中断。
 * 主要流程：
 *   1. 重新确保 GPIOA 和 SYSCFG 时钟已开启。
 *   2. 将 PA0 映射到 EXTI0。
 *   3. 清除 EXTI/NVIC 残留挂起位。
 *   4. 按原理图将 WK_UP 配置为下降沿触发：平时 R6 上拉到 3V3，
 *      按下 K2 后被拉到 DGND。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   无返回值。
 */
void bsp_wkup_key_exti_init(void)
{
    rcu_periph_clock_enable(KEYA_CLK_PORT);
    rcu_periph_clock_enable(RCU_SYSCFG);

    gpio_mode_set(KEYA_PORT, GPIO_MODE_INPUT, GPIO_PUPD_PULLUP, KEYW_PIN);

    syscfg_exti_line_config(EXTI_SOURCE_GPIOA, EXTI_SOURCE_PIN0);

    /*
     * 进入 WFI 前必须同时清 EXTI 和 NVIC pending。
     * 如果上一次按键释放或配置过程留下挂起位，内核可能在真正睡下前
     * 立即消费掉旧中断，后续按键下降沿就不再唤醒本次深睡。
     */
    exti_interrupt_flag_clear(EXTI_0);
    NVIC_ClearPendingIRQ(EXTI0_IRQn);

    /*
     * WK_UP 电路为外部 10K 上拉、按下接地，因此有效唤醒边沿是下降沿。
     * 只开下降沿可避免释放按键时又产生一次无意义的 EXTI0 中断。
     */
    exti_init(EXTI_0, EXTI_INTERRUPT, EXTI_TRIG_FALLING);
    nvic_irq_enable(EXTI0_IRQn, 1U, 0U);
}

/*
 * 函数作用：
 *   在深度睡眠唤醒并完成外设重建后，关闭 EXTI0 唤醒中断并清理残留挂起位。
 * 主要流程：
 *   1. 先关闭 NVIC 中的 EXTI0 中断响应。
 *   2. 再关闭 EXTI 线本身的中断使能。
 *   3. 最后清除 EXTI 和 NVIC 的残留挂起位，避免后续普通按键使用阶段误触发。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   无返回值。
 */
void bsp_wkup_key_exti_deinit(void)
{
    nvic_irq_disable(EXTI0_IRQn);
    exti_interrupt_disable(EXTI_0);
    exti_interrupt_flag_clear(EXTI_0);
    NVIC_ClearPendingIRQ(EXTI0_IRQn);
}
