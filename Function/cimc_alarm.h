#ifndef CIMC_ALARM_H
#define CIMC_ALARM_H

/* 告警模块：超阈判断、1s去抖、最多10条记录、主动上报/仅记录两种模式 */

#define SYSTEM_ALL_BASE_ONLY
#include "system_all.h"
#undef SYSTEM_ALL_BASE_ONLY

#ifdef __cplusplus
extern "C" {
#endif

void    alm_init(void);   /* 初始化：清空记录，模式置为仅记录 */
void    alm_load(void);   /* 从Flash恢复历史记录，应在init()之后调用 */
void    alm_save(void);   /* 将RAM记录持久化到Flash，应在重启前调用 */

void    alm_set_mode(uint8_t mode);  /* 0x01=主动上报 / 0x02=仅记录 */
uint8_t alm_get_mode(void);

/* ADC采集后调用：channel超threshold且距上次>=1s时记录/上报 */
void    alm_check(uint8_t channel, float threshold, float value);

/* 将最近10条记录倒序写入buf，无记录时写"empty" */
void    alm_query(char *buf, uint16_t size);

void    alm_clear(void);  /* 清除所有记录并重置去抖计时器 */

#ifdef __cplusplus
}
#endif

#endif /* CIMC_ALARM_H */
