#ifndef CIMC_POWER_APP_H
#define CIMC_POWER_APP_H

/* 低功耗命令（0x03AA）接口：MCU深度睡眠10s后唤醒，恢复外设并发"instrument wakeup" */

#define SYSTEM_ALL_BASE_ONLY
#include "system_all.h"
#undef SYSTEM_ALL_BASE_ONLY

#ifdef __cplusplus
extern "C" {
#endif

/* 执行深度睡眠10s流程，唤醒后恢复外设并通过RS485发ASCII唤醒字符串 */
void cimc_power_sleep_10s(void);

#ifdef __cplusplus
}
#endif

#endif /* CIMC_POWER_APP_H */
