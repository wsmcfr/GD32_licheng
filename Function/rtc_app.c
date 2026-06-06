#include "rtc_app.h"

/*
 * 函数作用：
 *   周期性读取 RTC 当前时间并刷新驱动层共享时间缓存。
 * 主要流程：
 *   1. 调用驱动层 rtc_current_time_get() 更新 rtc_initpara。
 *   2. 不再写 OLED；正式版 OLED 只保留队伍编号和运行状态两行。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   无返回值。
 */
void rtc_task(void)
{
    rtc_current_time_get(&rtc_initpara);
}
