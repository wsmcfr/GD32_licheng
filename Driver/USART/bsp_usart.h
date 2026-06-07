#ifndef BSP_USART_H
#define BSP_USART_H

#define SYSTEM_ALL_BASE_ONLY
#include "system_all.h"
#undef SYSTEM_ALL_BASE_ONLY

#ifdef __cplusplus
extern "C" {
#endif

/* RS485通信串口，出厂默认波特率19200 */
#define RS485_PORT               USART1
#define RS485_BAUD            19200U

/* USART1/RS485 DMA接收缓冲区长度 */
#define BSP_USART1_RX_BUFFER_SIZE      1024U

/* USART1引脚与DMA映射 */
#define USART1_RDATA_ADDRESS           ((uint32_t)&USART_DATA(USART1))
#define USART1_RX_DMA_PERIPH           DMA0
#define USART1_RX_DMA_CHANNEL          DMA_CH5
#define USART1_RX_DMA_SUBPERI          DMA_SUBPERI4
#define USART1_TX_PORT                 GPIOD
#define USART1_CLK_PORT                RCU_GPIOD
#define USART1_TX_PIN                  GPIO_PIN_5
#define USART1_RX_PIN                  GPIO_PIN_6
#define USART1_AF                      GPIO_AF_7

/* RS485方向控制脚PE8（DE/RE#复用） */
#define RS485_USART                    RS485_PORT
#define RS485_DIR_PORT                 GPIOE
#define RS485_DIR_CLK_PORT             RCU_GPIOE
#define RS485_DIR_PIN                  GPIO_PIN_8
#define RS485_DIR_TX_LEVEL             SET
#define RS485_DIR_RX_LEVEL             RESET

extern uint8_t usart1_rxbuffer[BSP_USART1_RX_BUFFER_SIZE];

void     bsp_usart_init(void);          /* 初始化USART1/RS485 */

/* 阻塞发送字节流 */
uint16_t bsp_usart_send_buffer(uint32_t usart_periph, const uint8_t *data, uint16_t length);

/* 切换USART1波特率 */
void     bsp_usart_change_baudrate(uint32_t baudrate);

#ifdef __cplusplus
}
#endif

#endif /* BSP_USART_H */
