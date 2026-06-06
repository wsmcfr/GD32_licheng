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

#endif /* __RTC_APP_H_ */
