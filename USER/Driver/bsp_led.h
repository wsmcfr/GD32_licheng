#ifndef BSP_LED_H
#define BSP_LED_H

/*
 * 文件作用：
 *   定义板载 LED 的引脚资源、控制宏以及初始化接口。
 *   与 LED 相关的宏全部放在本头文件中，不再放到公共头文件里。
 */

#define SYSTEM_ALL_BASE_ONLY
#include "system_all.h"
#undef SYSTEM_ALL_BASE_ONLY

#ifdef __cplusplus
extern "C" {
#endif

/* LED 所在 GPIO 端口和时钟。 */
#define LED_PORT                GPIOD
#define LED_CLK_PORT            RCU_GPIOD

/* 六个 LED 的物理引脚定义。 */
#define LED1_PIN                GPIO_PIN_10
#define LED2_PIN                GPIO_PIN_11
#define LED3_PIN                GPIO_PIN_12
#define LED4_PIN                GPIO_PIN_13
#define LED5_PIN                GPIO_PIN_14
#define LED6_PIN                GPIO_PIN_15

/* LED 电平有效配置：1 表示高电平点亮，0 表示低电平点亮。 */
#define LED_ACTIVE_HIGH         1U

/*
 * 宏作用：
 *   按当前板级有效电平配置写入 LED 电平。
 *   这里统一封装后，上层不再关心高低电平点亮差异。
 */
#if LED_ACTIVE_HIGH
#define LED_WRITE(pin, on)                                                      \
    do {                                                                        \
        if (on) {                                                               \
            GPIO_BOP(LED_PORT) = (pin);                                         \
        } else {                                                                \
            GPIO_BC(LED_PORT) = (pin);                                          \
        }                                                                       \
    } while (0)
#else
#define LED_WRITE(pin, on)                                                      \
    do {                                                                        \
        if (on) {                                                               \
            GPIO_BC(LED_PORT) = (pin);                                          \
        } else {                                                                \
            GPIO_BOP(LED_PORT) = (pin);                                         \
        }                                                                       \
    } while (0)
#endif

/* 单个 LED 控制宏。 */
#define LED1_SET(state)         do { LED_WRITE(LED1_PIN, ((state) != 0U)); } while (0)
#define LED2_SET(state)         do { LED_WRITE(LED2_PIN, ((state) != 0U)); } while (0)
#define LED3_SET(state)         do { LED_WRITE(LED3_PIN, ((state) != 0U)); } while (0)
#define LED4_SET(state)         do { LED_WRITE(LED4_PIN, ((state) != 0U)); } while (0)
#define LED5_SET(state)         do { LED_WRITE(LED5_PIN, ((state) != 0U)); } while (0)
#define LED6_SET(state)         do { LED_WRITE(LED6_PIN, ((state) != 0U)); } while (0)

#define LED1_TOGGLE             do { GPIO_TG(LED_PORT) = LED1_PIN; } while (0)
#define LED2_TOGGLE             do { GPIO_TG(LED_PORT) = LED2_PIN; } while (0)
#define LED3_TOGGLE             do { GPIO_TG(LED_PORT) = LED3_PIN; } while (0)
#define LED4_TOGGLE             do { GPIO_TG(LED_PORT) = LED4_PIN; } while (0)
#define LED5_TOGGLE             do { GPIO_TG(LED_PORT) = LED5_PIN; } while (0)
#define LED6_TOGGLE             do { GPIO_TG(LED_PORT) = LED6_PIN; } while (0)

#define LED1_ON                 do { LED_WRITE(LED1_PIN, 1U); } while (0)
#define LED2_ON                 do { LED_WRITE(LED2_PIN, 1U); } while (0)
#define LED3_ON                 do { LED_WRITE(LED3_PIN, 1U); } while (0)
#define LED4_ON                 do { LED_WRITE(LED4_PIN, 1U); } while (0)
#define LED5_ON                 do { LED_WRITE(LED5_PIN, 1U); } while (0)
#define LED6_ON                 do { LED_WRITE(LED6_PIN, 1U); } while (0)

#define LED1_OFF                do { LED_WRITE(LED1_PIN, 0U); } while (0)
#define LED2_OFF                do { LED_WRITE(LED2_PIN, 0U); } while (0)
#define LED3_OFF                do { LED_WRITE(LED3_PIN, 0U); } while (0)
#define LED4_OFF                do { LED_WRITE(LED4_PIN, 0U); } while (0)
#define LED5_OFF                do { LED_WRITE(LED5_PIN, 0U); } while (0)
#define LED6_OFF                do { LED_WRITE(LED6_PIN, 0U); } while (0)

/*
 * 函数作用：
 *   初始化板载 LED 使用的 GPIO，并统一关闭所有 LED。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   无返回值。
 */
void bsp_led_init(void);

#ifdef __cplusplus
}
#endif

#endif /* BSP_LED_H */
