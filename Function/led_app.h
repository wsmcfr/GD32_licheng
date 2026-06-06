#ifndef __LED_APP_H__
#define __LED_APP_H__

#include "system_all.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * 函数作用：
 *   低功耗或复位前关闭正式版两个 LED 指示灯。
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
 */
void led_app_blank_for_sleep(void);

/*
 * 函数作用：
 *   复位 LED 应用层缓存，让下一次 led_task() 强制刷新两个正式指示灯。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   无返回值。
 */
void led_app_reset_cache(void);

/*
 * 函数作用：
 *   调度器周期调用的 LED 任务。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   无返回值。
 * 说明：
 *   LED1 从进入 APP 后以 1s 为单位闪烁；LED2 在自动采集上报过程常亮，其余熄灭。
 */
void led_task(void);

#ifdef __cplusplus
}
#endif

#endif /* __LED_APP_H__ */
