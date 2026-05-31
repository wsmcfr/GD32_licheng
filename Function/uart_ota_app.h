#ifndef UART_OTA_APP_H
#define UART_OTA_APP_H

/*
 * 文件作用：
 *   定义 RS485/USART1 头部 bin OTA 通道的共享缓冲区、状态复位接口和周期任务入口。
 * 说明：
 *   当前升级文件固定为 Project_ota.bin，格式为 64 字节 OTA 头部 + 原始 App payload。
 *   现场只需要把该文件按普通裸字节发送到 RS485/USART1。
 */

#include "system_all.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * 宏作用：
 *   定义 OTA 应用层接收缓冲区长度。
 * 说明：
 *   USART1 DMA 只作为流式搬运窗口，不再要求一帧放下完整文件。
 */
#if defined(BSP_USART1_RX_BUFFER_SIZE)
#define UART_OTA_FRAME_BUFFER_SIZE     BSP_USART1_RX_BUFFER_SIZE
#else
#define UART_OTA_FRAME_BUFFER_SIZE     512U
#endif

/*
 * 宏作用：
 *   定义 USART1 ISR 到 OTA 任务之间的环形队列槽数。
 * 说明：
 *   串口工具连续发送大文件时，DMA 满缓冲中断可能比 5ms 调度任务更频繁；
 *   多槽队列可以吸收短时间调度抖动，只有队列满时才记录丢段计数。
 */
#define UART_OTA_RX_QUEUE_DEPTH        4U

/*
 * 宏作用：
 *   定义头部 bin OTA 的固定头部长度和 payload 最大长度。
 * 说明：
 *   payload 最大长度必须与内部 Flash 下载缓存区保持一致；接收端先完整接收
 *   payload 到 RAM，再统一擦写下载区，避免 Flash 擦写期间丢串口数据。
 */
#define UART_OTA_IMAGE_HEADER_SIZE     64U
#define UART_OTA_PAYLOAD_BUFFER_SIZE   BOOTLOADER_PORT_DOWNLOAD_MAX_SIZE

/*
 * 变量作用：
 *   USART1/RS485 中断移交给 OTA 模块的环形队列、长度表和诊断计数。
 * 说明：
 *   队列由 USART1 IDLE 中断和 DMA 满缓冲中断写入，由 uart_ota_task() 读取。
 *   写入和读取索引必须在短临界区内更新，避免 ISR 与任务层同时修改。
 */
extern __IO uint8_t uart_ota_rx_flag;
extern __IO uint16_t uart_ota_dma_length[UART_OTA_RX_QUEUE_DEPTH];
extern uint8_t uart_ota_dma_buffer[UART_OTA_RX_QUEUE_DEPTH][UART_OTA_FRAME_BUFFER_SIZE];
extern __IO uint32_t uart_ota_irq_count;
extern __IO uint32_t uart_ota_overwrite_count;
extern __IO uint16_t uart_ota_last_irq_length;
extern __IO uint8_t uart_ota_queue_write_index;
extern __IO uint8_t uart_ota_queue_read_index;
extern __IO uint8_t uart_ota_queue_count;

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
 *   把 USART1/RS485 中断层收到的一段连续字节喂给头部 bin OTA 状态机。
 * 参数说明：
 *   data：本次 DMA/IDLE 捕获到的连续字节缓冲区。
 *   length：本次缓冲区有效字节数，单位为字节。
 * 返回值说明：
 *   无返回值。
 */
void uart_ota_feed_rx_bytes(const uint8_t *data, uint16_t length);

/*
 * 函数作用：
 *   周期性处理 RS485/USART1 OTA 完整接收后的 Flash 提交和升级交接。
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
