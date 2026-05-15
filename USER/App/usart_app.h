#ifndef __USART_APP_H__
#define __USART_APP_H__

#include "system_all.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * 宏作用：
 *   定义 USART0 应用层命令帧缓冲区长度。
 * 说明：
 *   USART0 当前承担 LittleFS 调试口，不再接收 OTA 帧，因此保留 1KB 文本命令缓冲
 *   用于承接一次 IDLE 帧即可。
 */
#if defined(BSP_USART0_RX_BUFFER_SIZE)
#define UART_APP_DMA_BUFFER_SIZE       BSP_USART0_RX_BUFFER_SIZE
#else
#define UART_APP_DMA_BUFFER_SIZE       1024U
#endif

/*
 * 变量作用：
 *   USART0 IDLE 中断接收到的一帧调试命令缓存，以及完成标志位。
 */
extern __IO uint8_t rx_flag;
extern __IO uint16_t uart_dma_length;
extern uint8_t uart_dma_buffer[UART_APP_DMA_BUFFER_SIZE];

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
 *   周期性处理 USART0 上收到的文本调试命令。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   无返回值。
 * 说明：
 *   当前命令集合同时覆盖 LittleFS 文件系统调试命令，以及 RTC 年月日时分秒的读取/设置命令。
 */
void uart_task(void);

#ifdef __cplusplus
}
#endif

#endif /* __USART_APP_H__ */
