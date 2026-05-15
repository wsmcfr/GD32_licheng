#ifndef __BTN_APP_H
#define __BTN_APP_H

#include "system_all.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * 函数作用：
 *   初始化按键扫描状态，建立简单轮询方案的稳定状态基线。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   无返回值。
 */
void app_btn_init(void);

/*
 * 函数作用：
 *   周期性轮询按键、执行轻量去抖，并在检测到按下沿后触发业务动作。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   无返回值。
 */
void btn_task(void);

#ifdef __cplusplus
}
#endif

#endif /* __BTN_APP_H */
