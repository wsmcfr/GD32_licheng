#ifndef __RTC_APP_H_
#define __RTC_APP_H_

#include "system_all.h"

/* Unix UTC秒级时间戳写入RTC*/
int rtc_app_set_unix_epoch(uint32_t unix_ts);

/* 读取RTC当前时间转换为Unix UTC秒级时间戳*/
int rtc_app_get_unix_epoch(uint32_t *unix_ts);

#endif /* __RTC_APP_H_ */
