#ifndef __RTC_APP_H_
#define __RTC_APP_H_

#include "system_all.h"

/*
 * 函数作用：
 *   周期性读取 RTC 当前时间并刷新驱动层共享时间缓存。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   无返回值。
 */
void rtc_task(void);

/*
 * 函数作用：
 *   将 Unix UTC 秒级时间戳转换为年月日时分秒后写入 RTC。
 *   用于赛题 0x0105 命令：上位机下发 4 字节 UTC 时间戳，设备更新 RTC 并回 OK。
 * 参数说明：
 *   unix_ts：Unix 纪元（1970-01-01 00:00:00 UTC）起的秒数。
 * 返回值说明：
 *   0：转换并写入成功。
 *  -1：RTC 写入失败。
 */
int rtc_app_set_unix_epoch(uint32_t unix_ts);

/*
 * 函数作用：
 *   读取 RTC 当前时间，转换为 Unix UTC 秒级时间戳输出。
 *   用于赛题 0x0106 命令：上位机查询当前时间，回复 4 字节 UTC 时间戳。
 * 参数说明：
 *   unix_ts：输出指针，成功时写入 Unix 纪元起的秒数；指针为空时直接返回 -1。
 * 返回值说明：
 *   0：读取并转换成功。
 *  -1：参数为空或 RTC 读取失败。
 */
int rtc_app_get_unix_epoch(uint32_t *unix_ts);

#endif /* __RTC_APP_H_ */
