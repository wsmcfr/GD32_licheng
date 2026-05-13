/************************************************************
 * 版权：2025CIMC Copyright。 
 * 文件：usart.h
 * 作者: Jialei Zhao
 * 平台: 2025CIMC IHD-V04
 * 版本: Jialei Zhao     2025/12/30     V0.01    original
************************************************************/
#ifndef __USART_H__
#define __USART_H__
/************************* 头文件 *************************/

#include "HeaderFiles.h"


/************************* 宏定义 *************************/
#define USART_PORT GPIOA
#define USART USART0
#define USART_TX_Pin GPIO_PIN_9
#define USART_RX_Pin GPIO_PIN_10
#define USART_RCU RCU_USART0
#define USART_PIN_RCU RCU_GPIOA

/* BootLoader 与 App 共用同一调试/升级串口，默认波特率必须保持一致。 */
#define USART_DEFAULT_BAUDRATE 460800U

/************************ 变量定义 ************************/


/************************ 函数定义 ************************/

/*
 * 函数作用：
 *   初始化 BootLoader 使用的 USART0，包括 GPIO、外设时钟、波特率和接收中断。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   无返回值。
 */
void my_usart_init(void);

/*
 * 函数作用：
 *   处理通过中断接收到的一帧串口数据。
 *   当前实现仅用于调试打印，不属于 BootLoader 主升级链路的关键步骤。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   无返回值。
 */
void usart_recv_buf(void);

#endif // !__USART_H__

/****************************End*****************************/
