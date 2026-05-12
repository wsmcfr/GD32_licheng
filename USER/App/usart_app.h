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
extern __IO uint16_t uart_dma_length;
extern uint8_t uart_dma_buffer[UART_APP_DMA_BUFFER_SIZE];

/*
 * 变量作用：
 *   RS485/USART1 IDLE 中断接收到的一帧数据缓存、有效长度和完成标志。
 * 说明：
 *   USART1 使用独立缓存，避免和 USART0 默认串口转发缓存互相覆盖。
 */
extern __IO uint8_t rs485_rx_flag;
extern __IO uint16_t rs485_dma_length;
extern uint8_t rs485_dma_buffer[UART_APP_DMA_BUFFER_SIZE];

/*
 * 函数作用：
 *   向指定串口输出格式化字符串。
 * 参数说明：
 *   usart_periph：目标 USART 外设编号，例如 DEBUG_USART。
 *   format：printf 风格格式字符串，后续可变参数必须与格式占位符匹配。
 * 返回值说明：
 *   非负值：vsnprintf 生成的完整字符数，实际发送内容可能因缓冲区大小被截断。
 *   负值：格式化失败。
 */
int my_printf(uint32_t usart_periph, const char *format, ...);

/*
 * 函数作用：
 *   周期性处理 USART0 与 RS485 的双向透明转发。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   无返回值。
 */
void uart_task(void);

#ifdef __cplusplus
}
#endif

#endif /* __USART_APP_H__ */
