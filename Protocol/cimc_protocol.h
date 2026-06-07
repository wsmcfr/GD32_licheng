#ifndef CIMC_PROTOCOL_H
#define CIMC_PROTOCOL_H

/* CIMC串口协议入口：帧解析、CRC校验、应答组帧、命令分发 */

#define SYSTEM_ALL_BASE_ONLY
#include "system_all.h"
#undef SYSTEM_ALL_BASE_ONLY

#ifdef __cplusplus
extern "C" {
#endif

/* 处理RS485收到的一帧ASCII十六进制协议数据，返回1=已处理，0=非法帧 */
uint8_t proto_rx(const uint8_t *frame, uint16_t length);

/* 调度器100ms周期调用，自动上报激活时按间隔推送数据帧 */
void proto_tick(void);

/* 发送开机心跳帧（类型0x05，命令字0x8888），通知上位机本机在线 */
void proto_hb(void);

#ifdef __cplusplus
}
#endif

#endif /* CIMC_PROTOCOL_H */
