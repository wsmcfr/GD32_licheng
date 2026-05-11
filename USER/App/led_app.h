#ifndef __LED_APP_H__
#define __LED_APP_H__

#include "system_all.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * 变量作用：
 *   保存 6 个 LED 的逻辑显示状态，1 表示点亮，0 表示熄灭。
 */
extern uint8_t ucLed[6];

/*
 * 函数作用：
 *   按照 ucLed 数组内容刷新 LED 状态。
 */
void led_task(void);

#ifdef __cplusplus
}
#endif

#endif
