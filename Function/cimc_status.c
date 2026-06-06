#include "cimc_status.h"

/*
 * 变量作用：
 *   保存当前 APP 是否处于自动采集上报状态。
 * 说明：
 *   LED 采集工作灯和 OLED 第二行都读取该状态，避免两个模块各自维护状态造成显示不一致。
 */
static volatile uint8_t g_cimc_auto_sample_active = 0U;

/*
 * 函数作用：
 *   获取当前固件使用的队伍编号字符串。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   返回以 '\0' 结尾的队伍编号字符串常量，调用方不能修改该字符串内容。
 */
const char *cimc_status_get_team_id(void)
{
    return CIMC_TEAM_ID_TEXT;
}

/*
 * 函数作用：
 *   设置当前是否处于自动采集上报状态。
 * 参数说明：
 *   active：非 0 表示处于自动采集上报过程，0 表示空闲或其他非自动采集状态。
 * 返回值说明：
 *   无返回值。
 */
void cimc_status_set_auto_sample(uint8_t active)
{
    g_cimc_auto_sample_active = (active != 0U) ? 1U : 0U;
}

/*
 * 函数作用：
 *   查询当前是否处于自动采集上报状态。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   1：表示当前处于自动采集上报过程。
 *   0：表示当前不处于自动采集上报过程。
 */
uint8_t cimc_status_is_auto_sample_active(void)
{
    return g_cimc_auto_sample_active;
}
