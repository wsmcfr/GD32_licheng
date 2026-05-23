#ifndef UART_OTA_APP_H
#define UART_OTA_APP_H

/*
 * 文件作用：
 *   定义 RS485/USART1 OTA 通道的共享缓冲区、状态复位接口和周期任务入口。
 * 说明：
 *   该模块只处理 START/DATA/END 分包协议，不参与 USART0 与 RS485 的普通桥接。
 */

#include "system_all.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * 宏作用：
 *   定义 OTA 应用层接收缓冲区长度。
 * 说明：
 *   OTA 数据现在专门从 RS485/USART1 进入，因此这里直接跟随 Driver 层
 *   USART1 DMA 缓冲长度，保证一个 DATA 帧能够完整放入。
 */
#if defined(BSP_USART1_RX_BUFFER_SIZE)
#define UART_OTA_FRAME_BUFFER_SIZE     BSP_USART1_RX_BUFFER_SIZE
#else
#define UART_OTA_FRAME_BUFFER_SIZE     1024U
#endif

/*
 * 变量作用：
 *   USART1/RS485 IDLE 中断移交给 OTA 模块的一帧数据、有效长度和完成标志。
 * 说明：
 *   OTA 模块使用独立共享缓冲区，避免和 USART0 调试命令数据互相覆盖。
 */
extern __IO uint8_t uart_ota_rx_flag;
extern __IO uint16_t uart_ota_dma_length;
extern uint8_t uart_ota_dma_buffer[UART_OTA_FRAME_BUFFER_SIZE];
extern __IO uint32_t uart_ota_irq_count;
extern __IO uint32_t uart_ota_overwrite_count;
extern __IO uint16_t uart_ota_last_irq_length;

/*
 * 函数作用：
 *   向 RS485/USART1 OTA 专用口发送一次上电探测串，便于现场确认 OTA 线缆和串口号。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   无返回值。
 */
void uart_ota_emit_startup_probe(void);

/*
 * 函数作用：
 *   复位 OTA 运行态缓存和会话状态。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   无返回值。
 */
void uart_ota_reset_runtime(void);

/*
 * 函数作用：
 *   周期性处理 RS485/USART1 OTA 分包接收、ACK 回包和升级交接。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   无返回值。
 */
void uart_ota_task(void);

#ifdef __cplusplus
}
#endif

#endif /* UART_OTA_APP_H */
