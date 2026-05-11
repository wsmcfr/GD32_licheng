#ifndef __RTC_APP_H_
#define __RTC_APP_H_

#include "system_all.h"

/*
 * 函数作用：
 *   周期性读取 RTC 当前时间，并显示到 OLED 第 4 行。
 */
void rtc_task(void);

#endif /* __RTC_APP_H_ */
