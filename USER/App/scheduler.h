#ifndef SCHEDULER_H
#define SCHEDULER_H

#include "system_all.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * 函数作用：
 *   完成系统启动阶段的所有底层外设和应用模块初始化。
 */
void system_init(void);

/*
 * 函数作用：
 *   轮询调度所有周期任务。
 */
void scheduler_run(void);

#ifdef __cplusplus
}
#endif

#endif
