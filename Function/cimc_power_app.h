#ifndef CIMC_POWER_APP_H
#define CIMC_POWER_APP_H

/*
 * 文件作用：
 *   赛题 J-01 低功耗命令（0x03AA）的应用层接口。
 *   MCU 进入深度睡眠（Deepsleep），由 RTC 自动唤醒定时器 10s 后唤醒，
 *   唤醒后恢复外设并向 RS485 发送 ASCII 字符串 "instrument wakeup"。
 */

#define SYSTEM_ALL_BASE_ONLY
#include "system_all.h"
#undef SYSTEM_ALL_BASE_ONLY

#ifdef __cplusplus
extern "C" {
#endif

/*
 * 函数作用：
 *   执行赛题 J-01 睡眠流程：
 *   1. 关闭 LED，停止 SysTick，减少唤醒前活跃电流。
 *   2. 配置 RTC 自动唤醒定时器为 10 秒（ck_spre 1Hz，计数值 9）。
 *   3. 配置 EXTI_22（RTC 唤醒线）上升沿中断，允许其作为唤醒源。
 *   4. 进入 PMU 深度睡眠（WFI），等待 RTC 唤醒事件。
 *   5. 唤醒后：恢复系统时钟（SystemInit），重新初始化 SysTick 和 USART，
 *      重置调度器基准时刻，向 RS485 发送 "instrument wakeup" 字符串。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   无返回值；函数在唤醒并完成恢复后正常返回。
 */
void cimc_power_sleep_10s(void);

#ifdef __cplusplus
}
#endif

#endif /* CIMC_POWER_APP_H */
