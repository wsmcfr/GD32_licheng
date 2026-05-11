#ifndef __USART_APP_H__
#define __USART_APP_H__

#include "system_all.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * 宏作用：
 *   定义应用层用于暂存一帧串口数据的缓冲区长度。
 * 说明：
 *   这里使用独立的 App 层宏，避免聚合头循环包含时依赖 Driver 头中的长度宏。
 */
#define UART_APP_DMA_BUFFER_SIZE       512U

/*
 * 变量作用：
 *   串口 IDLE 中断接收到的一帧数据缓存，以及完成标志位。
 */
extern __IO uint8_t rx_flag;
extern uint8_t uart_dma_buffer[UART_APP_DMA_BUFFER_SIZE];

/*
 * 函数作用：
 *   向指定串口输出格式化字符串。
 * 返回值说明：
 *   返回实际输出的字符数。
 */
int my_printf(uint32_t usart_periph, const char *format, ...);

/*
 * 函数作用：
 *   周期性处理收到的一帧串口数据并回显。
 */
void uart_task(void);

#ifdef __cplusplus
}
#endif

#endif /* __USART_APP_H__ */
