#ifndef __OLED_H__
#define __OLED_H__

#include "system_all.h"

#ifdef __cplusplus
extern "C" {
#endif

#define OLED_WIDTH  128
#define OLED_HEIGHT 32

/* 显示16px ASCII字符串。 */
uint8_t OLED_ShowStr(uint8_t x, uint8_t y, char *ch);

/* 初始化OLED控制器并清屏。 */
void OLED_Init(void);

#ifdef __cplusplus
  }
#endif

#endif  /*__OLED_H__*/
