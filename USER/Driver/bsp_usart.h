#ifndef BSP_USART_H
#define BSP_USART_H

/*
 * 文件作用：
 *   定义调试串口和 DMA 接收相关的硬件资源、共享缓冲区和初始化接口。
 *   串口相关宏统一集中在本头文件，不再放在公共头文件里。
 */

#define SYSTEM_ALL_BASE_ONLY
#include "system_all.h"
#undef SYSTEM_ALL_BASE_ONLY

#ifdef __cplusplus
extern "C" {
#endif

/* 调试串口选择。 */
#define DEBUG_USART                    USART0

/* 接收缓冲区长度定义。 */
#define BSP_USART0_RX_BUFFER_SIZE      512U
#define BSP_USART1_RX_BUFFER_SIZE      256U
#define BSP_USART2_RX_BUFFER_SIZE      256U
#define BSP_USART5_RX_BUFFER_SIZE      256U

/* USART0 引脚与 DMA 映射。 */
#define USART0_RDATA_ADDRESS           ((uint32_t)&USART_DATA(USART0))
#define USART0_RX_DMA_PERIPH           DMA1
#define USART0_RX_DMA_CHANNEL          DMA_CH5
#define USART0_RX_DMA_SUBPERI          DMA_SUBPERI4
#define USART0_TX_PORT                 GPIOA
#define USART0_RX_PORT                 GPIOA
#define USART0_CLK_PORT                RCU_GPIOA
#define USART0_TX_PIN                  GPIO_PIN_9
#define USART0_RX_PIN                  GPIO_PIN_10
#define USART0_AF                      GPIO_AF_7

/* USART1 引脚与 DMA 映射。 */
#define USART1_RDATA_ADDRESS           ((uint32_t)&USART_DATA(USART1))
#define USART1_RX_DMA_PERIPH           DMA0
#define USART1_RX_DMA_CHANNEL          DMA_CH5
#define USART1_RX_DMA_SUBPERI          DMA_SUBPERI4
#define USART1_TX_PORT                 GPIOD
#define USART1_RX_PORT                 GPIOD
#define USART1_CLK_PORT                RCU_GPIOD
#define USART1_TX_PIN                  GPIO_PIN_5
#define USART1_RX_PIN                  GPIO_PIN_6
#define USART1_AF                      GPIO_AF_7

/* USART2 引脚与 DMA 映射。 */
#define USART2_RDATA_ADDRESS           ((uint32_t)&USART_DATA(USART2))
#define USART2_RX_DMA_PERIPH           DMA0
#define USART2_RX_DMA_CHANNEL          DMA_CH1
#define USART2_RX_DMA_SUBPERI          DMA_SUBPERI4
#define USART2_TX_PORT                 GPIOD
#define USART2_RX_PORT                 GPIOD
#define USART2_CLK_PORT                RCU_GPIOD
#define USART2_TX_PIN                  GPIO_PIN_8
#define USART2_RX_PIN                  GPIO_PIN_9
#define USART2_AF                      GPIO_AF_7

/* USART5 引脚与 DMA 映射。 */
#define USART5_RDATA_ADDRESS           ((uint32_t)&USART_DATA(USART5))
#define USART5_RX_DMA_PERIPH           DMA1
#define USART5_RX_DMA_CHANNEL          DMA_CH1
#define USART5_RX_DMA_SUBPERI          DMA_SUBPERI5
#define USART5_TX_PORT                 GPIOC
#define USART5_RX_PORT                 GPIOC
#define USART5_CLK_PORT                RCU_GPIOC
#define USART5_TX_PIN                  GPIO_PIN_6
#define USART5_RX_PIN                  GPIO_PIN_7
#define USART5_AF                      GPIO_AF_8

/* 各串口 DMA 接收缓冲区，由驱动层统一提供。 */
extern uint8_t usart0_rxbuffer[BSP_USART0_RX_BUFFER_SIZE];
extern uint8_t usart1_rxbuffer[BSP_USART1_RX_BUFFER_SIZE];
extern uint8_t usart2_rxbuffer[BSP_USART2_RX_BUFFER_SIZE];
extern uint8_t usart5_rxbuffer[BSP_USART5_RX_BUFFER_SIZE];

/* 串口初始化接口。 */
void bsp_usart0_init(void);
void bsp_usart_init(void);
void bsp_usart1_init(void);
void bsp_usart2_init(void);
void bsp_usart5_init(void);
void bsp_usart_all_init(void);

#ifdef __cplusplus
}
#endif

#endif /* BSP_USART_H */
