#ifndef BSP_LED_H
#define BSP_LED_H

#define SYSTEM_ALL_BASE_ONLY
#include "system_all.h"
#undef SYSTEM_ALL_BASE_ONLY

#ifdef __cplusplus
extern "C" {
#endif

/* LED 所在 GPIO 端口和时钟。 */
#define LED_PORT                GPIOD
#define LED_CLK_PORT            RCU_GPIOD

/* 正式版只使用两个 LED：LED1 为系统状态灯，LED2 为采集工作灯。 */
#define LED1_PIN                GPIO_PIN_10
#define LED2_PIN                GPIO_PIN_11

/* LED 电平有效配置：1 表示高电平点亮，0 表示低电平点亮。 */
#define LED_ACTIVE_HIGH         1U

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

#define LED1_OFF                do { LED_WRITE(LED1_PIN, 0U); } while (0)
#define LED2_OFF                do { LED_WRITE(LED2_PIN, 0U); } while (0)

void bsp_led_init(void);

#ifdef __cplusplus
}
#endif

#endif /* BSP_LED_H */
