/* Licence
* Company: MCUSTUDIO
* Auther: Ahypnis.
* Version: V0.10
* Time: 2025/06/05
* Note:
*/

#include "mcu_cmic_gd32f470vet6.h"

extern uint16_t adc_value[2];

/**
 * @brief	使用类似printf的方式显示字符串，显示6x8大小的ASCII字符
 * @param x  Character position on the X-axis  range：0 - 127
 * @param y  Character position on the Y-axis  range：0 - 3
 * 例如：oled_printf(0, 0, "Data = %d", dat);
 **/
int oled_printf(uint8_t x, uint8_t y, const char *format, ...)
{
  char buffer[512]; // 临时存储格式化后的字符串
  va_list arg;      // 处理可变参数
  int len;          // 最终字符串长度

  va_start(arg, format);
  // 安全地格式化字符串到 buffer
  len = vsnprintf(buffer, sizeof(buffer), format, arg);
  va_end(arg);

  OLED_ShowStr(x, y, buffer, 8);
  return len;
}

void oled_task(void)
{
    oled_printf(0, 0, "KEY STA: %d%d%d%d%d%d", KEY1_READ, KEY2_READ, KEY3_READ, KEY4_READ, KEY5_READ, KEY6_READ);
    oled_printf(0, 1, "uwTick:%lld", (long long)get_system_ms());
    oled_printf(0, 2, "A0:%.2fV V:%.2f", adc_value[0] / 4095.0f * 3.3f, adc_value[1] / 4095.0f * 3.3f);
	
}

/* CUSTOM EDIT */
