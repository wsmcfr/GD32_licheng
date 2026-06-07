#ifndef __RTC_APP_H_
#define __RTC_APP_H_

#include "system_all.h"

/* 1s周期：读取RTC时间并刷新驱动层共享缓存 */
void rtc_task(void);

/* 将Unix UTC秒级时间戳写入RTC，成功返回0，失败返回-1 */
int rtc_app_set_unix_epoch(uint32_t unix_ts);

/* 读取RTC当前时间转换为Unix UTC秒级时间戳，成功返回0，失败返回-1 */
int rtc_app_get_unix_epoch(uint32_t *unix_ts);

#endif /* __RTC_APP_H_ */
