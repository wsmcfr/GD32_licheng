#ifndef BSP_POWER_H
#define BSP_POWER_H

/*
 * 文件作用：
 *   提供 Sleep、Deep-sleep 和 Standby 三档低功耗入口，以及对应的
 *   板级资源收拢与恢复接口。
 */

#define SYSTEM_ALL_BASE_ONLY
#include "system_all.h"
#undef SYSTEM_ALL_BASE_ONLY

#ifdef __cplusplus
extern "C" {
#endif

/*
 * 函数作用：
 *   进入 Sleep 轻量睡眠模式，只停止 CPU 执行并保留当前外设运行态。
 * 主要流程：
 *   1. 配置 KEYW/PA0 的 EXTI0 中断作为本次 Sleep 唤醒源。
 *   2. 暂停 SysTick，避免 1ms 节拍立刻把 CPU 唤醒。
 *   3. 调用 PMU Sleep WFI 入口，唤醒后恢复 timebase 和调度器基线。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   无返回值；函数会在 KEYW 唤醒后继续执行恢复流程。
 */
void bsp_enter_sleep(void);

/*
 * 函数作用：
 *   关闭当前不需要的外设并进入 Deep-sleep 深度睡眠，唤醒后重新初始化板级资源。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   无返回值；函数会在 WFI 唤醒后继续执行恢复流程。
 */
void bsp_enter_deepsleep(void);

/*
 * 函数作用：
 *   收拢外设后进入 Standby 待机模式，使用 KEYW/PA0 的 PMU WKUP 功能唤醒。
 * 主要流程：
 *   1. 关闭外设、总线和 GPIO，降低 Standby 前的板级残留功耗。
 *   2. 清除 PMU 唤醒/待机标志并启用 WKUP 引脚。
 *   3. 调用 PMU Standby WFI 入口。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   无返回值；Standby 唤醒后按复位流程重新启动，正常情况下本函数不会返回。
 */
void bsp_enter_standby(void);

#ifdef __cplusplus
}
#endif

#endif /* BSP_POWER_H */
