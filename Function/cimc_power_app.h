#ifndef CIMC_POWER_APP_H
#define CIMC_POWER_APP_H


#define SYSTEM_ALL_BASE_ONLY
#include "system_all.h"
#undef SYSTEM_ALL_BASE_ONLY

#ifdef __cplusplus
extern "C" {
#endif

/* 执行深度睡眠10s流程 */
void power_sleep(void);

#ifdef __cplusplus
}
#endif

#endif /* CIMC_POWER_APP_H */
