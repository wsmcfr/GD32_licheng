#ifndef __OLED_H__
#define __OLED_H__

#include "system_all.h"

#ifdef __cplusplus
extern "C" {
#endif

#define OLED_ADDR 0x78

#define OLED_WIDTH  128
#define OLED_HEIGHT 32

/* 发送1字节命令；成功返回1，失败返回0 */
uint8_t OLED_Write_cmd(uint8_t cmd);

/* 连续发送多字节命令，DMA自动分块；length=0视为成功，失败返回0 */
uint8_t OLED_Write_cmd_buf(const uint8_t *cmds, uint16_t length);

/* 写入1字节显存数据；成功返回1，失败返回0 */
uint8_t OLED_Write_data(uint8_t data);

/* 连续写入多字节显存，DMA自动分块；length=0视为成功，失败返回0 */
uint8_t OLED_Write_data_buf(const uint8_t *data, uint16_t length);

/* 在指定矩形区域(x0,y0)-(x1,y1)显示位图 */
void OLED_ShowPic(uint8_t x0, uint8_t y0, uint8_t x1, uint8_t y1, uint8_t BMP[]);

/* 在(x,y)处显示字库中第no个汉字 */
void OLED_ShowHanzi(uint8_t x, uint8_t y, uint8_t no);

/* 在(x,y)处显示大号汉字字库中第n个字 */
void OLED_ShowHzbig(uint8_t x, uint8_t y, uint8_t n);

/* 在(x,y)处显示浮点数，accuracy指定小数位数，fontsize选择字模尺寸 */
void OLED_ShowFloat(uint8_t x, uint8_t y, float num, uint8_t accuracy, uint8_t fontsize);

/* 在(x,y)处显示无符号整数，length为固定位数，fontsize选择字模尺寸 */
void OLED_ShowNum(uint8_t x, uint8_t y, uint32_t num, uint8_t length, uint8_t fontsize);

/* 在(x,y)处显示字符串；成功返回1，越界或写入失败返回0 */
uint8_t OLED_ShowStr(uint8_t x, uint8_t y, char *ch, uint8_t fontsize);

/* 在(x,y)处显示单个ASCII字符，fontsize选择字模尺寸 */
void OLED_ShowChar(uint8_t x, uint8_t y, uint8_t ch, uint8_t fontsize);

/* 填满整块OLED显示区域 */
void OLED_Allfill(void);

/* 设置后续写入的显示坐标(x,y)；成功返回1，失败返回0 */
uint8_t OLED_Set_Position(uint8_t x, uint8_t y);

/* 清空OLED显示内容 */
void OLED_Clear(void);

/* 打开OLED显示输出 */
void OLED_Display_On(void);

/* 关闭OLED显示输出（息屏/低功耗） */
void OLED_Display_Off(void);

/* 初始化OLED控制器并清屏 */
void OLED_Init(void);

#ifdef __cplusplus
  }
#endif

#endif  /*__OLED_H__*/
