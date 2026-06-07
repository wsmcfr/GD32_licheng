#ifndef CIMC_PROTOCOL_H
#define CIMC_PROTOCOL_H

#define SYSTEM_ALL_BASE_ONLY
#include "system_all.h"
#undef SYSTEM_ALL_BASE_ONLY

#ifdef __cplusplus
extern "C" {
#endif

/* 处理RS485收到协议数据 */
uint8_t proto_rx(const uint8_t *frame, uint16_t length);
void proto_tick(void);
/* 发送开机心跳帧 */
void proto_hb(void);

#ifdef __cplusplus
}
#endif

#endif /* CIMC_PROTOCOL_H */
