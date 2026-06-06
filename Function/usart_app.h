#ifndef __USART_APP_H__
#define __USART_APP_H__

#include "system_all.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * 宏作用：
 *   控制正式版是否允许通过 my_printf 输出调试日志。
 * 说明：
 *   赛题正式通信只允许 USART1/RS485 承载协议数据，默认关闭调试日志，避免日志插入评分串口。
 *   若现场排障需要临时输出，可在编译选项中定义 CIMC_DEBUG_LOG_ENABLE=1。
 */
#ifndef CIMC_DEBUG_LOG_ENABLE
#define CIMC_DEBUG_LOG_ENABLE          0U
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
 * 函数作用：
 *   向指定串口输出格式化字符串。
 * 参数说明：
 *   usart_periph：目标 USART 外设编号；正式版默认忽略该参数并丢弃日志。
 *   format：printf 风格格式字符串，后续可变参数必须与格式占位符匹配。
 * 返回值说明：
 *   非负值：vsnprintf 生成的完整字符数。
 *   负值：格式化失败。
 * 说明：
 *   正式评测默认关闭实际发送，防止调试日志污染 USART1/RS485 协议链路。
 */
int my_printf(uint32_t usart_periph, const char *format, ...);

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
