#ifndef UART_OTA_YMODEM_H
#define UART_OTA_YMODEM_H

/*
 * 文件作用：
 *   定义 RS485/USART1 OTA 通道的 YModem 接收接口。
 * 说明：
 *   YModem 只负责把串口工具发送的 Project.bin 写入下载缓存区；
 *   真正的软件复位仍由 uart_ota_app.c 统一触发，BootLoader 搬运流程保持不变。
 */

#include "system_all.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * 宏作用：
 *   定义 YModem 接收模块返回给 uart_ota_task() 的处理结果。
 * 说明：
 *   数值故意与 uart_ota_app.c 内部的 uart_ota_result_t 保持一致，
 *   这样 uart_ota_task() 可以用同一套成功、消费、等待和失败分支处理两种 OTA 协议。
 */
#define UART_OTA_YMODEM_RESULT_NOT_PACKET      0U
#define UART_OTA_YMODEM_RESULT_SUCCESS         1U
#define UART_OTA_YMODEM_RESULT_BAD_LENGTH      2U
#define UART_OTA_YMODEM_RESULT_BAD_VECTOR      3U
#define UART_OTA_YMODEM_RESULT_FLASH_ERROR     4U
#define UART_OTA_YMODEM_RESULT_VERIFY_ERROR    5U
#define UART_OTA_YMODEM_RESULT_WAIT_MORE       6U
#define UART_OTA_YMODEM_RESULT_FRAME_CONSUMED  7U

/*
 * 函数作用：
 *   复位 YModem OTA 会话状态，并允许空闲态继续周期性发送 'C' 请求。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   无返回值。
 */
void uart_ota_ymodem_reset_runtime(void);

/*
 * 函数作用：
 *   周期性维护 YModem 接收状态。
 * 主要流程：
 *   1. 空闲时定期发送字符 'C'，提示上位机进入 CRC 模式发送。
 *   2. 等待最终空包期间，如果串口工具不发送结束空包，则超时后交给外层复位。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   UART_OTA_YMODEM_RESULT_SUCCESS：固件已接收并写好参数区，外层可以复位。
 *   UART_OTA_YMODEM_RESULT_FRAME_CONSUMED：本次只做了维护动作，不需要复位。
 */
uint8_t uart_ota_ymodem_send_poll(void);

/*
 * 函数作用：
 *   尝试把 USART1/RS485 收到的一帧原始数据按 YModem 协议解析。
 * 参数说明：
 *   packet：USART1 IDLE 中断移交的一帧原始字节。
 *   packet_length：packet 的有效字节数。
 * 返回值说明：
 *   UART_OTA_YMODEM_RESULT_SUCCESS：YModem 文件接收完成，外层可以复位。
 *   UART_OTA_YMODEM_RESULT_FRAME_CONSUMED：当前 YModem 帧已处理完毕。
 *   UART_OTA_YMODEM_RESULT_NOT_PACKET：当前数据不是 YModem 帧。
 *   其它返回值：当前 YModem 会话发生长度、校验、向量表或 Flash 错误。
 */
uint8_t uart_ota_ymodem_try_process_packet(const uint8_t *packet, uint32_t packet_length);

#ifdef __cplusplus
}
#endif

#endif /* UART_OTA_YMODEM_H */
