#ifndef CIMC_PROTOCOL_H
#define CIMC_PROTOCOL_H

/*
 * 文件作用：
 *   定义 2026 CIMC 初赛串口协议入口，包括帧解析、CRC 校验、应答组帧，
 *   以及 A/B 模块系统基础命令、心跳帧、DAC、自动采集和 Bootloader 升级命令分发。
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
 *   1：该帧已被识别并处理（含已发送应答或静默丢弃）。
 *   0：参数无效或内容不足以解析为任何协议帧。
 */
uint8_t cimc_protocol_process_ascii_frame(const uint8_t *frame, uint16_t length);

/*
 * 函数作用：
 *   由调度器周期调用（建议 100ms），在自动上报激活期间按 report_interval 推送数据帧。
 *   内部自行管理计时，未到达间隔时立即返回，无副作用。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   无返回值。
 */
void cimc_protocol_auto_report_tick(void);

/*
 * 函数作用：
 *   主动发送一帧心跳帧（帧类型 0x05，命令字 0x8888）。
 *   赛题要求设备上电/复位后立即发送心跳，告知上位机当前设备地址已在线。
 *   自动测评 A-03 "等待重启后心跳"依赖此帧：15s 内收到心跳且 ID 一致才视为通过。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   无返回值。
 */
void cimc_protocol_send_heartbeat(void);

#ifdef __cplusplus
}
#endif

#endif /* CIMC_PROTOCOL_H */
