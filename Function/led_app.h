#ifndef __LED_APP_H__
#define __LED_APP_H__

#include "system_all.h"

#ifdef __cplusplus
extern "C" {
#endif

void led_app_blank_for_sleep(void); /* 睡眠前熄灯 */
void led_app_reset_cache(void);     /* 复位缓存 */
void led_task(void);                /* LED任务 */

#ifdef __cplusplus
}
#endif

#endif /* __LED_APP_H__ */
