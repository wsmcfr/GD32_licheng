#ifndef CIMC_PROTOCOL_H
#define CIMC_PROTOCOL_H

/*
 * 文件作用：
 *   定义 2026 CIMC 初赛串口协议的最小正式入口。
 *   本模块负责 ASCII 十六进制帧解析、CRC 校验、应答组帧，以及当前已接入的
 *   DAC、自动采集状态和 Bootloader 升级请求命令分发。
 */

#define SYSTEM_ALL_BASE_ONLY
#include "system_all.h"
#undef SYSTEM_ALL_BASE_ONLY

#ifdef __cplusplus
extern "C" {
#endif

/*
 * 函数作用：
 *   处理 USART1/RS485 收到的一帧 ASCII 十六进制赛题协议数据。
 * 参数说明：
 *   frame：USART1 IDLE 中断移交的原始 ASCII 数据缓冲区，可以包含空格、回车或换行。
 *   length：frame 中有效字节数，单位为字节。
 * 返回值说明：
 *   1：表示该帧已被识别并处理，可能已经发送应答。
 *   0：表示参数无效、不是本设备帧或帧格式不足以处理。
 */
uint8_t cimc_protocol_process_ascii_frame(const uint8_t *frame, uint16_t length);

#ifdef __cplusplus
}
#endif

#endif /* CIMC_PROTOCOL_H */
