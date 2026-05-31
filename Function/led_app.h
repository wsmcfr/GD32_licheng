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
 *   设置单个 LED 的应用层逻辑状态，并立即同步到硬件输出。
 * 参数说明：
 *   led_index：LED 索引，0~5 分别对应 LED1~LED6；超出范围时忽略。
 *   state：非 0 表示点亮，0 表示熄灭。
 * 返回值说明：
 *   无返回值。
 */
void led_app_set(uint8_t led_index, uint8_t state);

/*
 * 函数作用：
 *   翻转单个 LED 的应用层逻辑状态，并立即同步到硬件输出。
 * 参数说明：
 *   led_index：LED 索引，0~5 分别对应 LED1~LED6；超出范围时忽略。
 * 返回值说明：
 *   无返回值。
 */
void led_app_toggle(uint8_t led_index);

/*
 * 函数作用：
 *   将所有 LED 的应用层状态设置为熄灭，并立即同步到硬件输出。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   无返回值。
 */
void led_app_all_off(void);

/*
 * 函数作用：
 *   低功耗入口专用熄灯接口，关闭硬件 LED 并复位 LED 刷新缓存。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   无返回值。
 * 说明：
 *   低功耗流程可能直接关闭 GPIO/时钟，必须让 LED app 缓存失效，唤醒后才能强制重刷。
 */
void led_app_blank_for_sleep(void);

/*
 * 函数作用：
 *   复位 LED 应用层的硬件刷新缓存，让下一次 led_task() 强制写入 6 路 LED。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   无返回值。
 */
void led_app_reset_cache(void);

/*
 * 函数作用：
 *   按照 ucLed 数组内容刷新 LED 状态。
 * 参数说明：
 *   无参数，函数内部直接读取全局 ucLed 状态数组。
 * 返回值说明：
 *   无返回值。
 */
void led_task(void);

#ifdef __cplusplus
}
#endif

#endif
