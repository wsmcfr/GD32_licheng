#ifndef SCHEDULER_H
#define SCHEDULER_H

#include "system_all.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * 函数作用：
 *   完成系统启动阶段的所有底层外设和应用模块初始化。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   无返回值。
 */
void system_init(void);

/*
 * 函数作用：
 *   轮询调度所有周期任务。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   无返回值。
 */
void scheduler_run(void);

/*
 * 函数作用：
 *   在深度睡眠唤醒或系统 timebase 被补偿后，重置所有周期任务的运行基线。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   无返回值。
 * 说明：
 *   该接口会把所有任务的 last_run 设置为当前毫秒 tick，避免唤醒后第一轮调度
 *   因睡眠期间 tick 间隔过长而集中执行所有任务。
 */
void scheduler_reset_runtime(void);

#ifdef __cplusplus
}
#endif

#endif
