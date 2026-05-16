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

/*
 * 宏作用：
 *   定义当前工程默认调试串口波特率。
 * 说明：
 *   USART0 现在只承担启动日志和 LittleFS 调试命令，不再接收 OTA 帧；
 *   为保持现有终端脚本和日志观察习惯，继续沿用 460800。
 */
#define DEBUG_USART_BAUDRATE           460800U

/*
 * 宏作用：
 *   定义 OTA 专用串口和默认波特率。
 * 说明：
 *   OTA 现在通过 RS485/USART1 收发；PC 工具、App 和未来如需调整的 BootLoader
 *   配置，应统一围绕这一组常量更新。USART0 仍只负责日志和调试命令。
 */
#define UART_OTA_USART                 RS485_USART
#define UART_OTA_USART_BAUDRATE        460800U

/* 接收缓冲区长度定义。 */
/*
 * 宏作用：
 *   定义 USART0 DMA 接收缓冲区长度。
 * 说明：
 *   USART0 只承担 LittleFS 调试口和日志终端，不再接收 OTA 数据，因此 1KB
 *   文本命令缓冲足够覆盖当前单帧命令调试需求。
 */
#define BSP_USART0_RX_BUFFER_SIZE      1024U
/*
 * USART1/RS485 当前承载 OTA 流式 DATA 帧，必须能放下 512B 载荷 + 24B 协议头。
 * 保留 1KB 余量也能覆盖上位机一帧中混入少量串口驱动附加字节的情况。
 */
#define BSP_USART1_RX_BUFFER_SIZE      1024U
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

/* RS485 方向控制脚：PE8 同时控制 MAX3485 的 DE 和 RE#，高电平发送，低电平接收。 */
#define RS485_USART                    USART1
#define RS485_DIR_PORT                 GPIOE
#define RS485_DIR_CLK_PORT             RCU_GPIOE
#define RS485_DIR_PIN                  GPIO_PIN_8
/* RS485 方向控制有效电平。若实测 485_CS 为低电平发送，只需交换下面两个宏。 */
#define RS485_DIR_TX_LEVEL             SET
#define RS485_DIR_RX_LEVEL             RESET

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
extern uint8_t usart5_rxbuffer[BSP_USART5_RX_BUFFER_SIZE];

/*
 * 函数作用：
 *   初始化 USART0 调试串口和 DMA 接收链路。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   无返回值。
 */
void bsp_usart0_init(void);

/*
 * 函数作用：
 *   初始化当前默认启用的 USART0 和 USART1/RS485 资源。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   无返回值。
 */
void bsp_usart_init(void);

/*
 * 函数作用：
 *   初始化 USART1 以及 RS485 方向控制 GPIO。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   无返回值。
 */
void bsp_usart1_init(void);

/*
 * 函数作用：
 *   初始化 USART5 及其 DMA 接收链路。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   无返回值。
 */
void bsp_usart5_init(void);

/*
 * 函数作用：
 *   初始化工程中定义的全部 USART 接口。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   无返回值。
 */
void bsp_usart_all_init(void);

/*
 * 函数作用：
 *   通过阻塞轮询方式向指定串口发送一段原始字节流。
 * 参数说明：
 *   usart_periph：目标 USART 外设编号。
 *   data：待发送数据起始地址，必须指向至少 length 字节的有效缓冲区。
 *   length：待发送字节数，单位为字节。
 * 返回值说明：
 *   返回实际完成发送流程的字节数；若等待标志超时，则返回超时前已发送长度。
 */
uint16_t bsp_usart_send_buffer(uint32_t usart_periph, const uint8_t *data, uint16_t length);

/*
 * 函数作用：
 *   将 RS485 收发器方向切换为接收态。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   无返回值。
 */
void bsp_rs485_direction_receive(void);

/*
 * 函数作用：
 *   将 RS485 收发器方向切换为发送态。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   无返回值。
 */
void bsp_rs485_direction_transmit(void);

#ifdef __cplusplus
}
#endif

#endif /* BSP_USART_H */
