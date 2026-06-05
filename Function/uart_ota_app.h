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
 *   定义 USART1/RS485 连续裸流 OTA 的 DMA 环形缓冲区大小。
 * 说明：
 *   上位机只能无停顿连续发送 Project_ota.bin，所以 USART1 RX 必须使用
 *   circular DMA，让 Flash 编程期间进入的字节先落到 SRAM 环形缓冲区。
 *   32KB 在 115200 8N1 下约等于 2.8 秒输入余量，用来覆盖单块 Flash 编程
 *   和调度抖动；下载区整区擦除必须在 ready 前完成，不能占用这段余量。
 */
#define UART_OTA_RING_BUFFER_SIZE    BSP_USART1_RX_BUFFER_SIZE

/*
 * 宏作用：
 *   定义任务层每次从 DMA 环形缓冲取出的最大连续处理窗口。
 * 说明：
 *   该窗口只作为短暂栈缓冲，替代原来的 128KB 整包 payload RAM。
 *   512B 可以让单次 Flash 编程阻塞时间保持较短，同时减少函数调用开销。
 */
#define UART_OTA_STREAM_WINDOW_SIZE  512U

/*
 * 变量作用：
 *   USART1/RS485 circular DMA 接收环形缓冲区和诊断计数。
 * 说明：
 *   DMA 硬件持续写入 uart_ota_ring_buffer，任务层根据 DMA 剩余计数计算硬件
 *   写指针并推进读指针。中断只用于唤醒/诊断，不再复制数据或重装 DMA。
 */
extern __IO uint8_t uart_ota_rx_flag;
extern __IO uint32_t uart_ota_irq_count;
extern __IO uint32_t uart_ota_overwrite_count;
extern __IO uint16_t uart_ota_last_irq_length;
extern __IO uint32_t uart_ota_ring_read_index;

/*
 * 函数作用：
 *   在对外发送 OTA ready 探测串之前预擦内部 Flash 下载区。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   1：下载区预擦成功，可以开始接收连续裸流。
 *   0：下载区预擦失败，不能提示上位机发送升级文件。
 */
uint8_t uart_ota_prepare_download_area_before_ready(void);

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
