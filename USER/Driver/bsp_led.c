#include "bsp_led.h"

/*
 * 函数作用：
 *   初始化板载 LED 的 GPIO 输出模式。
 * 主要流程：
 *   1. 打开 GPIOD 时钟。
 *   2. 将 6 个 LED 引脚配置为推挽输出。
 *   3. 统一关闭所有 LED，避免上电残留状态。
 */
void bsp_led_init(void)
{
    rcu_periph_clock_enable(LED_CLK_PORT);

    gpio_mode_set(LED_PORT,
                  GPIO_MODE_OUTPUT,
                  GPIO_PUPD_PULLUP,
                  LED1_PIN | LED2_PIN | LED3_PIN | LED4_PIN | LED5_PIN | LED6_PIN);
    gpio_output_options_set(LED_PORT,
                            GPIO_OTYPE_PP,
                            GPIO_OSPEED_50MHZ,
                            LED1_PIN | LED2_PIN | LED3_PIN | LED4_PIN | LED5_PIN | LED6_PIN);

    LED1_OFF;
    LED2_OFF;
    LED3_OFF;
    LED4_OFF;
    LED5_OFF;
    LED6_OFF;
}
