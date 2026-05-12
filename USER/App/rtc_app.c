#include "rtc_app.h"

/*
 * 函数作用：
 *   周期性读取 RTC 当前时间，并把时分秒显示到 OLED 第 4 行。
 * 主要流程：
 *   1. 调用驱动层 rtc_current_time_get() 更新 rtc_initpara。
 *   2. 将 BCD/十六进制格式的时分秒按两位宽度输出到 OLED。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   无返回值。
 */
void rtc_task(void)
{
    rtc_current_time_get(&rtc_initpara);

    oled_printf(0, 3, "%0.2x:%0.2x:%0.2x", rtc_initpara.hour, rtc_initpara.minute, rtc_initpara.second);
}
