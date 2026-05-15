#ifndef __RS485_APP_H__
#define __RS485_APP_H__

#include "system_all.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * 宏作用：
 *   定义 RS485 应用层帧缓冲区长度。
 * 说明：
 *   当前直接复用 USART1 DMA 接收缓冲区大小，保证 ISR 与任务层对单帧最大长度的理解一致。
 */
#if defined(BSP_USART1_RX_BUFFER_SIZE)
#define RS485_APP_BUFFER_SIZE          BSP_USART1_RX_BUFFER_SIZE
#else
#define RS485_APP_BUFFER_SIZE          256U
#endif

/*
 * 变量作用：
 *   USART1/RS485 一帧接收完成后的共享缓冲区、有效长度和完成标志。
 * 说明：
 *   `USART1_IRQHandler()` 负责写入，`rs485_task()` 负责消费并清零，沿用工程现有
 *   ISR-to-task handoff 模式。
 */
extern __IO uint8_t rs485_rx_flag;
extern __IO uint16_t rs485_dma_length;
extern uint8_t rs485_dma_buffer[RS485_APP_BUFFER_SIZE];

/*
 * 函数作用：
 *   周期性处理 USART1/RS485 上收到的一整帧数据，并通过 USART1 原样回显。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   无返回值。
 */
void rs485_task(void);

#ifdef __cplusplus
}
#endif

#endif /* __RS485_APP_H__ */
