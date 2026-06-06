#ifndef CIMC_STATUS_H
#define CIMC_STATUS_H

#include "system_all.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * 宏作用：
 *   定义本队伍在 OLED 第一行显示的队伍编号。
 * 说明：
 *   赛题要求 OLED 第一行始终显示队伍编号，后续如果需要修改队伍号，只改这里即可。
 */
#define CIMC_TEAM_ID_TEXT       "2026706296"

/*
 * 函数作用：
 *   获取当前固件使用的队伍编号字符串。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   返回以 '\0' 结尾的队伍编号字符串常量，调用方不能修改该字符串内容。
 */
const char *cimc_status_get_team_id(void);

/*
 * 函数作用：
 *   设置当前是否处于自动采集上报状态。
 * 参数说明：
 *   active：非 0 表示处于自动采集上报过程，0 表示空闲或其他非自动采集状态。
 * 返回值说明：
 *   无返回值。
 */
void cimc_status_set_auto_sample(uint8_t active);

/*
 * 函数作用：
 *   查询当前是否处于自动采集上报状态。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   1：表示当前处于自动采集上报过程。
 *   0：表示当前不处于自动采集上报过程。
 */
uint8_t cimc_status_is_auto_sample_active(void);

#ifdef __cplusplus
}
#endif

#endif /* CIMC_STATUS_H */
