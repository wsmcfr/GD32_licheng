#ifndef __USART_APP_H__
#define __USART_APP_H__

#include "system_all.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * 宏作用：
 *   定义 USART1/RS485 应用层帧缓存长度。
 * 说明：
 *   该长度与底层 DMA 缓冲一致，任务层后续解析赛题 ASCII 十六进制协议时从这里取帧。
 */
#define UART_APP_DMA_BUFFER_SIZE       BSP_USART1_RX_BUFFER_SIZE

/*
 * 变量作用：
 *   USART1/RS485 IDLE 中断接收到的一帧 ASCII 协议数据缓存，以及完成标志位。
 */
extern __IO uint8_t rx_flag;
extern __IO uint16_t uart_dma_length;
extern uint8_t uart_dma_buffer[UART_APP_DMA_BUFFER_SIZE];

/*
 * 变量作用：
 *   IDLE 中断去抖机制共享变量。ISR 只记录最后一次 IDLE 时刻，
 *   uart_task 等待 3ms 去抖后再取出 DMA 数据处理。
 *   解决 115200 波特率下 USB 转串口芯片的帧间隙（~1ms）
 *   被 USART IDLE 检测误判为帧结束的问题。
 */
extern __IO uint32_t g_usart_idle_tick;
extern __IO uint8_t  g_usart_idle_pending;

/*
 * 函数作用：
 *   从 USART1/RS485 共享接收缓冲区取走一帧数据。
 * 参数说明：
 *   output：调用方提供的输出缓冲区，必须非空。
 *   output_size：输出缓冲区容量，单位为字节；函数会预留 1 字节给 '\0'。
 * 返回值说明：
 *   大于 0：成功取出的有效字节数。
 *   0：当前没有新帧，或参数无效。
 */
uint16_t uart_app_take_frame(uint8_t *output, uint16_t output_size);

/*
 * 函数作用：
 *   周期性处理 USART1/RS485 上收到的赛题协议帧。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   无返回值。
 * 说明：
 *   当前入口已经接入赛题协议解析模块，正式版不再处理 USART0/SMARTFS Shell 文本命令。
 */
void uart_task(void);

#ifdef __cplusplus
}
#endif

#endif /* __USART_APP_H__ */
