#ifndef CIMC_ALARM_H
#define CIMC_ALARM_H

/*
 * 文件作用：
 *   定义 CIMC 告警模块接口，负责：
 *   - 超阈值判断与去抖（每通道至少 1s 间隔，避免 50ms ADC 任务刷爆记录）
 *   - 最多 10 条告警记录的 RAM 存储（Flash 持久化留待后续扩展）
 *   - 主动上报模式：超阈时以 ASCII 字符串直接发送到 RS485（非帧封装）
 *   - 被动记录模式：仅存储，0x0602 查询时返回
 *   - 记录清除（0x0603）
 */

#define SYSTEM_ALL_BASE_ONLY
#include "system_all.h"
#undef SYSTEM_ALL_BASE_ONLY

#ifdef __cplusplus
extern "C" {
#endif

/*
 * 函数作用：
 *   初始化告警模块：清空记录表，将模式重置为仅记录（0x02）。
 *   应在 system_init() 中 cimc_params_load() 之后调用。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   无返回值。
 */
void cimc_alarm_init(void);

/*
 * 函数作用：
 *   从 Flash user_config 区恢复历史告警记录到 RAM。
 *   应在 cimc_alarm_init() 之后调用，确保 RAM 清零后再叠加 Flash 数据。
 *   若 Flash 中魔术字不匹配或 CRC32 校验失败，则静默忽略（视为无历史记录）。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   无返回值。
 */
void cimc_alarm_load(void);

/*
 * 函数作用：
 *   设置告警上报模式。
 *   0x01 = 主动上报（超阈时立即发 ASCII 字符串且存记录）。
 *   0x02 = 仅记录（存记录，0x0602 查询时才返回）。
 * 参数说明：
 *   mode：0x01 或 0x02，其他值忽略。
 * 返回值说明：
 *   无返回值。
 */
void cimc_alarm_set_mode(uint8_t mode);

/*
 * 函数作用：
 *   获取当前告警上报模式。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   0x01：主动上报模式。
 *   0x02：仅记录模式。
 */
uint8_t cimc_alarm_get_mode(void);

/*
 * 函数作用：
 *   每次 ADC 采集后调用，判断指定通道是否超阈值。
 *   包含 1s 去抖：同一通道连续超阈时每秒最多产生 1 条记录，防止刷屏。
 *   若当前为主动上报模式，超阈时同时向 RS485 发送 ASCII 告警字符串。
 * 参数说明：
 *   channel：0 = CH0（电位器），1 = CH1（DAC 回读）。
 *   threshold：该通道的告警阈值（乘以变比后的工程值）。
 *   value：该通道当前实际值（乘以变比后的工程值）。
 * 返回值说明：
 *   无返回值。
 */
void cimc_alarm_check(uint8_t channel, float threshold, float value);

/*
 * 函数作用：
 *   将最近 10 条告警记录（时间倒序）以 ASCII 字符串形式写入调用方提供的缓冲区。
 *   若没有任何记录，写入字符串 "empty"（不含换行）。
 * 参数说明：
 *   buf：输出缓冲区，由调用方分配，建议至少 600 字节（10 条 × 55 字符）。
 *   size：缓冲区容量，单位字节；函数保证不越界写入。
 * 返回值说明：
 *   无返回值。
 */
void cimc_alarm_query(char *buf, uint16_t size);

/*
 * 函数作用：
 *   清除所有告警记录，将计数归零。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   无返回值。
 */
void cimc_alarm_clear(void);

/*
 * 函数作用：
 *   将当前 RAM 告警记录持久化到 Flash user_config 区。
 *   应在设备重启前（重启命令、波特率切换、OTA 升级请求）调用，
 *   不应在 cimc_alarm_check() 热路径中调用（Flash 整页擦写最长 1.5s，会阻塞 CPU）。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   无返回值。
 */
void cimc_alarm_save(void);

#ifdef __cplusplus
}
#endif

#endif /* CIMC_ALARM_H */
