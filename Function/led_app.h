#ifndef __LED_APP_H__
#define __LED_APP_H__

#include "system_all.h"

#ifdef __cplusplus
extern "C" {
#endif

void led_app_all_off(void);          /* 低功耗/复位前关闭两路LED */
void led_app_blank_for_sleep(void);  /* 睡眠前熄灯并复位缓存 */
void led_app_reset_cache(void);      /* 复位刷新缓存，下次led_task强制刷新 */
void led_task(void);                 /* 20ms周期：LED1每1s翻转，LED2跟随采集状态 */

#ifdef __cplusplus
}
#endif

#endif /* __LED_APP_H__ */
