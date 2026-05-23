#include "oled_app.h"

/*
 * 函数作用：
 *   使用 printf 风格格式化文本，并把结果显示到 OLED 指定坐标。
 * 主要流程：
 *   1. 使用 vsnprintf 将可变参数格式化到本地缓冲区，避免无界写入。
 *   2. 调用 OLED_ShowStr 按 6x8 ASCII 字模显示。
 * 参数说明：
 *   x：OLED 横向像素坐标，当前 128x32 屏建议范围为 0~127。
 *   y：OLED 行号，当前 6x8 字符显示建议范围为 0~3。
 *   format：printf 风格格式字符串，后续可变参数必须与格式占位符匹配。
 * 返回值说明：
 *   非负值：vsnprintf 计算出的格式化字符串长度，不包含结尾 '\0'。
 *   负值：格式化失败，返回值由 vsnprintf 决定。
 */
int oled_printf(uint8_t x, uint8_t y, const char *format, ...)
{
  char buffer[512]; /* 临时存储格式化后的字符串，长度与当前调试显示需求匹配。 */
  va_list arg;      /* 保存可变参数遍历状态，必须与 va_start/va_end 成对使用。 */
  int len;          /* 记录格式化结果长度，调用者可据此判断是否被截断。 */

  va_start(arg, format);
  /* 使用有界格式化，避免 OLED 显示文本超过临时缓冲区导致内存覆盖。 */
  len = vsnprintf(buffer, sizeof(buffer), format, arg);
  va_end(arg);

  OLED_ShowStr(x, y, buffer, 8);
  return len;
}

/*
 * 函数作用：
 *   周期性刷新 OLED 上的按键状态、系统运行时间和 ADC 电压值。
 * 主要流程：
 *   1. 第 1 行显示 6 个普通按键的实时 GPIO 电平。
 *   2. 第 2 行显示系统毫秒时基，便于观察调度器是否运行。
 *   3. 第 3 行把 ADC 原始值换算成电压显示。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   无返回值。
 */
void oled_task(void)
{
    oled_printf(0, 0, "KEY STA: %d%d%d%d%d%d", KEY1_READ, KEY2_READ, KEY3_READ, KEY4_READ, KEY5_READ, KEY6_READ);
    oled_printf(0, 1, "uwTick:%lld", (long long)get_system_ms());
    oled_printf(0, 2, "A0:%.2fV V:%.2f", adc_value[0] / 4095.0f * 3.3f, adc_value[1] / 4095.0f * 3.3f);
	
}
