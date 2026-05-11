#include "bsp_key.h"

/*
 * 函数作用：
 *   初始化全部按键 GPIO。
 * 主要流程：
 *   1. 打开三个 GPIO 端口时钟。
 *   2. 将所有按键引脚配置为上拉输入。
 * 说明：
 *   使用上拉输入是因为当前按键硬件接法按下时会拉低电平。
 */
void bsp_btn_init(void)
{
    rcu_periph_clock_enable(KEYB_CLK_PORT);
    rcu_periph_clock_enable(KEYE_CLK_PORT);
    rcu_periph_clock_enable(KEYA_CLK_PORT);

    gpio_mode_set(KEYE_PORT,
                  GPIO_MODE_INPUT,
                  GPIO_PUPD_PULLUP,
                  KEY1_PIN | KEY2_PIN | KEY3_PIN | KEY4_PIN | KEY5_PIN);
    gpio_mode_set(KEYB_PORT, GPIO_MODE_INPUT, GPIO_PUPD_PULLUP, KEY6_PIN);
    gpio_mode_set(KEYA_PORT, GPIO_MODE_INPUT, GPIO_PUPD_PULLUP, KEYW_PIN);
}

/*
 * 函数作用：
 *   配置唤醒按键对应的 EXTI0 外部中断。
 * 主要流程：
 *   1. 重新确保 GPIOA 和 SYSCFG 时钟已开启。
 *   2. 将 PA0 映射到 EXTI0。
 *   3. 配置双边沿触发，支持按下和释放都唤醒。
 */
void bsp_wkup_key_exti_init(void)
{
    rcu_periph_clock_enable(KEYA_CLK_PORT);
    rcu_periph_clock_enable(RCU_SYSCFG);

    gpio_mode_set(KEYA_PORT, GPIO_MODE_INPUT, GPIO_PUPD_PULLUP, KEYW_PIN);

    syscfg_exti_line_config(EXTI_SOURCE_GPIOA, EXTI_SOURCE_PIN0);
    exti_init(EXTI_0, EXTI_INTERRUPT, EXTI_TRIG_BOTH);
    exti_interrupt_flag_clear(EXTI_0);
    nvic_irq_enable(EXTI0_IRQn, 1U, 0U);
}
