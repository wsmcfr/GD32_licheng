#ifndef __BTN_APP_H
#define __BTN_APP_H

#include "system_all.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * 函数作用：
 *   初始化按键事件框架以及按键回调关系。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   无返回值。
 */
void app_btn_init(void);

/*
 * 函数作用：
 *   周期性处理按键状态机。
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
