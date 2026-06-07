#ifndef __USART_APP_H__
#define __USART_APP_H__

#include "system_all.h"

#ifdef __cplusplus
extern "C" {
#endif

#define UART_APP_DMA_BUFFER_SIZE  BSP_USART1_RX_BUFFER_SIZE

/* IDLE去抖共享变量 */
extern __IO uint32_t g_idle_ms;
extern __IO uint8_t  g_idle_pend;

void     uart_task(void);

#ifdef __cplusplus
}
#endif

#endif /* __USART_APP_H__ */
