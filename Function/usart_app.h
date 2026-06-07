#ifndef __USART_APP_H__
#define __USART_APP_H__

#include "system_all.h"

#ifdef __cplusplus
extern "C" {
#endif

#define UART_APP_DMA_BUFFER_SIZE  BSP_USART1_RX_BUFFER_SIZE

extern __IO uint8_t  rx_flag;
extern __IO uint16_t uart_dma_length;
extern uint8_t       uart_dma_buffer[UART_APP_DMA_BUFFER_SIZE];

/* IDLE去抖共享变量：ISR写，uart_task读；3ms去抖防USB分包误判 */
extern __IO uint32_t g_usart_idle_tick;
extern __IO uint8_t  g_usart_idle_pending;

uint16_t uart_app_take_frame(uint8_t *output, uint16_t output_size); /* 从DMA缓冲取一帧 */
void     uart_task(void); /* 5ms周期：IDLE去抖后取帧并交给协议解析 */

#ifdef __cplusplus
}
#endif

#endif /* __USART_APP_H__ */
