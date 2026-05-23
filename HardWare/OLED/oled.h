#ifndef __OLED_H__
#define __OLED_H__

#include "system_all.h"

#ifdef __cplusplus
extern "C" {
#endif

#define OLED_ADDR 0x78

#define OLED_WIDTH 128
#define OLED_HEIGHT 32

/*
 * 函数作用：
 *   向 OLED 控制器发送 1 字节命令。
 * 参数说明：
 *   cmd：需要写入 OLED 控制器的命令字节。
 * 返回值说明：
 *   无返回值。
 */
void OLED_Write_cmd(uint8_t cmd);

/*
 * 函数作用：
 *   向 OLED 显存写入 1 字节显示数据。
 * 参数说明：
 *   data：需要写入 OLED 显存的数据字节。
 * 返回值说明：
 *   无返回值。
 */
void OLED_Write_data(uint8_t data);

/*
 * 函数作用：
 *   在指定矩形区域显示一幅位图。
 * 参数说明：
 *   x0：位图左上角横向起始坐标。
 *   y0：位图左上角页起始坐标。
 *   x1：位图右下角横向结束坐标。
 *   y1：位图右下角页结束坐标。
 *   BMP：位图数据数组指针，调用者需保证数据长度与显示区域匹配。
 * 返回值说明：
 *   无返回值。
 */
void OLED_ShowPic(uint8_t x0, uint8_t y0, uint8_t x1, uint8_t y1, uint8_t BMP[]);

/*
 * 函数作用：
 *   在 OLED 指定位置显示一个汉字字模。
 * 参数说明：
 *   x：显示起始横坐标。
 *   y：显示起始页坐标。
 *   no：字库中的汉字序号。
 * 返回值说明：
 *   无返回值。
 */
void OLED_ShowHanzi(uint8_t x, uint8_t y, uint8_t no);

/*
 * 函数作用：
 *   在 OLED 指定位置显示一个大号汉字字模。
 * 参数说明：
 *   x：显示起始横坐标。
 *   y：显示起始页坐标。
 *   n：大号汉字字库中的序号。
 * 返回值说明：
 *   无返回值。
 */
void OLED_ShowHzbig(uint8_t x, uint8_t y, uint8_t n);

/*
 * 函数作用：
 *   在 OLED 指定位置显示浮点数。
 * 参数说明：
 *   x：显示起始横坐标。
 *   y：显示起始页坐标。
 *   num：待显示的浮点数。
 *   accuracy：小数位数。
 *   fontsize：字体高度或字模尺寸选择。
 * 返回值说明：
 *   无返回值。
 */
void OLED_ShowFloat(uint8_t x, uint8_t y, float num, uint8_t accuracy, uint8_t fontsize);

/*
 * 函数作用：
 *   在 OLED 指定位置显示无符号整数。
 * 参数说明：
 *   x：显示起始横坐标。
 *   y：显示起始页坐标。
 *   num：待显示的无符号整数。
 *   length：固定显示位数。
 *   fontsize：字体高度或字模尺寸选择。
 * 返回值说明：
 *   无返回值。
 */
void OLED_ShowNum(uint8_t x, uint8_t y, uint32_t num, uint8_t length, uint8_t fontsize);

/*
 * 函数作用：
 *   在 OLED 指定位置显示字符串。
 * 参数说明：
 *   x：显示起始横坐标。
 *   y：显示起始页坐标。
 *   ch：待显示字符串指针，需以 '\0' 结束。
 *   fontsize：字体高度或字模尺寸选择。
 * 返回值说明：
 *   无返回值。
 */
void OLED_ShowStr(uint8_t x, uint8_t y, char *ch, uint8_t fontsize);

/*
 * 函数作用：
 *   在 OLED 指定位置显示单个 ASCII 字符。
 * 参数说明：
 *   x：显示起始横坐标。
 *   y：显示起始页坐标。
 *   ch：待显示字符的 ASCII 编码。
 *   fontsize：字体高度或字模尺寸选择。
 * 返回值说明：
 *   无返回值。
 */
void OLED_ShowChar(uint8_t x, uint8_t y, uint8_t ch, uint8_t fontsize);

/*
 * 函数作用：
 *   填满整块 OLED 显示区域。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   无返回值。
 */
void OLED_Allfill(void);

/*
 * 函数作用：
 *   设置 OLED 后续写入的显示坐标。
 * 参数说明：
 *   x：横向像素坐标。
 *   y：页坐标。
 * 返回值说明：
 *   无返回值。
 */
void OLED_Set_Position(uint8_t x, uint8_t y);

/*
 * 函数作用：
 *   清空 OLED 显示内容。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   无返回值。
 */
void OLED_Clear(void);

/*
 * 函数作用：
 *   打开 OLED 显示输出。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   无返回值。
 */
void OLED_Display_On(void);

/*
 * 函数作用：
 *   关闭 OLED 显示输出，用于息屏或低功耗前收拢显示器状态。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   无返回值。
 */
void OLED_Display_Off(void);

/*
 * 函数作用：
 *   初始化 OLED 控制器并清屏，准备后续显示输出。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   无返回值。
 */
void OLED_Init(void);

#ifdef __cplusplus
  }
#endif

#endif  /*__OLED_H__*/
