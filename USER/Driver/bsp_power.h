#ifndef BSP_POWER_H
#define BSP_POWER_H

/*
 * 文件作用：
 *   提供深度睡眠进入前后的板级资源收拢与恢复接口。
 */

#define SYSTEM_ALL_BASE_ONLY
#include "system_all.h"
#undef SYSTEM_ALL_BASE_ONLY

#ifdef __cplusplus
extern "C" {
#endif

/*
 * 函数作用：
 *   关闭当前不需要的外设并进入深度睡眠，唤醒后重新初始化板级资源。
 */
void bsp_enter_deepsleep(void);

#ifdef __cplusplus
}
#endif

#endif /* BSP_POWER_H */
