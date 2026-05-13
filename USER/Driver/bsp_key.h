#ifndef BSP_KEY_H
#define BSP_KEY_H

/*
 * 文件作用：
 *   定义板载按键的引脚映射、读取宏和初始化接口。
 *   所有按键相关宏都在本头文件内维护。
 */

#define SYSTEM_ALL_BASE_ONLY
#include "system_all.h"
#undef SYSTEM_ALL_BASE_ONLY

#ifdef __cplusplus
extern "C" {
#endif

/* 按键 GPIO 端口及时钟定义。 */
#define KEYE_PORT               GPIOE
#define KEYB_PORT               GPIOB
#define KEYA_PORT               GPIOA
#define KEYE_CLK_PORT           RCU_GPIOE
#define KEYB_CLK_PORT           RCU_GPIOB
#define KEYA_CLK_PORT           RCU_GPIOA

/* 六个普通按键和一个唤醒按键的引脚映射。 */
#define KEY1_PIN                GPIO_PIN_15
#define KEY2_PIN                GPIO_PIN_6
#define KEY3_PIN                GPIO_PIN_11
#define KEY4_PIN                GPIO_PIN_4
#define KEY5_PIN                GPIO_PIN_7
#define KEY6_PIN                GPIO_PIN_0
#define KEYW_PIN                GPIO_PIN_0

/* 按键电平读取宏。当前硬件上拉输入，按下通常读到 0。 */
#define KEY1_READ               gpio_input_bit_get(KEYE_PORT, KEY1_PIN)
#define KEY2_READ               gpio_input_bit_get(KEYE_PORT, KEY2_PIN)
#define KEY3_READ               gpio_input_bit_get(KEYE_PORT, KEY3_PIN)
#define KEY4_READ               gpio_input_bit_get(KEYE_PORT, KEY4_PIN)
#define KEY5_READ               gpio_input_bit_get(KEYE_PORT, KEY5_PIN)
#define KEY6_READ               gpio_input_bit_get(KEYB_PORT, KEY6_PIN)
#define KEYW_READ               gpio_input_bit_get(KEYA_PORT, KEYW_PIN)

/*
 * 函数作用：
 *   初始化所有普通按键和唤醒按键对应的 GPIO 输入模式。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   无返回值。
 */
void bsp_btn_init(void);

/*
 * 函数作用：
 *   将唤醒按键配置为 EXTI0 中断源，用于深度睡眠唤醒。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   无返回值。
 */
void bsp_wkup_key_exti_init(void);

/*
 * 函数作用：
 *   关闭唤醒按键对应的 EXTI0 中断源，避免系统恢复到正常运行态后仍保留低功耗专用唤醒路径。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   无返回值。
 */
void bsp_wkup_key_exti_deinit(void);

#ifdef __cplusplus
}
#endif

#endif /* BSP_KEY_H */
