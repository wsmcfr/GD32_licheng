#include "cimc_status.h"

/* 自动采集状态：LED2 和 OLED 第二行共同读取，统一控制 */
static volatile uint8_t g_cimc_auto_sample_active = 0;

// 返回队伍编号字符串常量，只读
const char *cimc_status_get_team_id(void)
{
    return CIMC_TEAM_ID_TEXT;
}

// 置位/清除自动上报标志，非0即为激活
void cimc_status_set_auto_sample(uint8_t active)
{
    g_cimc_auto_sample_active = (active != 0) ? 1 : 0;
}

/* 查询是否处于自动上报状态，1=激活 0=停止 */
uint8_t cimc_status_is_auto_sample_active(void)
{
    return g_cimc_auto_sample_active;
}
