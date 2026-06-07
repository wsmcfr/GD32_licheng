#ifndef CIMC_STATUS_H
#define CIMC_STATUS_H

#include "system_all.h"

#ifdef __cplusplus
extern "C" {
#endif

/* OLED第一行显示的队伍编号，修改队伍号只改这里 */
#define CIMC_TEAM_ID_TEXT       "2026706296"

const char *cimc_status_get_team_id(void);          /* 获取队伍编号字符串常量 */
void        cimc_status_set_auto_sample(uint8_t active); /* 设置自动上报状态 */
uint8_t     cimc_status_is_auto_sample_active(void);     /* 查询自动上报状态 */

#ifdef __cplusplus
}
#endif

#endif /* CIMC_STATUS_H */
