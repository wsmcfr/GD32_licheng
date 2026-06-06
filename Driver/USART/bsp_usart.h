#ifndef BSP_USART_H
#define BSP_USART_H

/*
 * 文件作用：
 *   定义正式比赛通信使用的 USART1/RS485 硬件资源、共享缓冲区和初始化接口。
 *   本工程正式版删除 USART0 和 USART5，避免非评分串口影响协议链路。
 */

#define SYSTEM_ALL_BASE_ONLY
#include "system_all.h"
#undef SYSTEM_ALL_BASE_ONLY

#ifdef __cplusplus
extern "C" {
#endif

/*
 * 宏作用：
 *   定义正式比赛通信串口和默认波特率。
 * 说明：
 *   赛题明确要求自动评分默认通过 USART1 的 RS485 接口通信，出厂默认波特率必须为 19200。
 */
#define CIMC_RS485_USART               USART1
#define CIMC_RS485_BAUDRATE            19200U

/*
 * 宏作用：
 *   定义 USART1/RS485 DMA 接收缓冲区长度。
 * 说明：
 *   比赛协议以 ASCII 十六进制字符串收发，单帧长度远小于 512 字节；
 *   这里保留 1KB 余量，供后续 0x0502 前的普通命令帧和异常帧处理使用。
 */
#define BSP_USART1_RX_BUFFER_SIZE      1024U

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
#define RS485_USART                    CIMC_RS485_USART
#define RS485_DIR_PORT                 GPIOE
#define RS485_DIR_CLK_PORT             RCU_GPIOE
#define RS485_DIR_PIN                  GPIO_PIN_8
/* RS485 方向控制有效电平。若实测 485_CS 为低电平发送，只需交换下面两个宏。 */
#define RS485_DIR_TX_LEVEL             SET
#define RS485_DIR_RX_LEVEL             RESET

/* USART1/RS485 DMA 接收缓冲区，由驱动层统一提供。 */
extern uint8_t usart1_rxbuffer[BSP_USART1_RX_BUFFER_SIZE];

/*
 * 函数作用：
 *   初始化当前正式版唯一启用的 USART1/RS485 资源。
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
