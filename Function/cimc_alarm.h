#ifndef CIMC_ALARM_H
#define CIMC_ALARM_H

/* 告警记录模块。 */

#define SYSTEM_ALL_BASE_ONLY
#include "system_all.h"
#undef SYSTEM_ALL_BASE_ONLY

#ifdef __cplusplus
extern "C" {
#endif

void    alm_init(void); /* 初始化 */
void    alm_load(void); /* 读Flash */
void    alm_save(void); /* 写Flash */

void    alm_set_mode(uint8_t mode); /* 01上报/02记录 */

void    alm_check(uint8_t channel, float threshold, float value); /* 超阈检查 */
void    alm_query(char *buf, uint16_t size); /* 查询记录 */
void    alm_clear(void); /* 清记录 */

#ifdef __cplusplus
}
#endif

#endif /* CIMC_ALARM_H */
