#ifndef CIMC_STATUS_H
#define CIMC_STATUS_H

#include "system_all.h"

#ifdef __cplusplus
extern "C" {
#endif

/*队伍编号 */
#define TEAM_ID       "2026706296"

const char *sts_team_id(void);          /* 获取队伍编号字符串常量 */
void        sts_set_sample(uint8_t active); /* 设置自动上报状态 */
uint8_t     sts_sampling(void);     /* 查询自动上报状态 */

#ifdef __cplusplus
}
#endif

#endif /* CIMC_STATUS_H */
