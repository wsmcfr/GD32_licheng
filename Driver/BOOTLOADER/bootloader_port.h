#ifndef BOOTLOADER_PORT_H
#define BOOTLOADER_PORT_H

#define SYSTEM_ALL_BASE_ONLY
#include "system_all.h"
#undef SYSTEM_ALL_BASE_ONLY

#ifdef __cplusplus
extern "C" {
#endif

#define BOOTLOADER_PORT_PARAM_ADDR         0x08010000UL
#define BOOTLOADER_PORT_PARAM_SIZE         (4U * 1024U)
#define BOOTLOADER_PORT_FLASH_PAGE_SIZE    4096U
#define BOOTLOADER_PORT_MAGIC_WORD         0xC0DEF47AUL
#define BOOTLOADER_PORT_TAIL_MAGIC         0xA5A5C3C3UL
#define BOOTLOADER_PORT_USER_CONFIG_SIZE   512U /* App参数区容量 */

typedef enum
{
    BOOTLOADER_PORT_STATUS_OK = 0,
    BOOTLOADER_PORT_STATUS_BAD_PARAM,
    BOOTLOADER_PORT_STATUS_FLASH_ERROR
} bootloader_port_status_t;

uint32_t bootloader_port_crc32_calc(const uint8_t *data, uint32_t length);

bootloader_port_status_t bootloader_port_request_bootloader_upgrade(void);

void bootloader_port_request_upgrade_reset(void);

bootloader_port_status_t bootloader_port_read_user_config(uint8_t *buf, uint16_t size);

bootloader_port_status_t bootloader_port_write_user_config(const uint8_t *buf, uint16_t size);

#ifdef __cplusplus
}
#endif

#endif /* BOOTLOADER_PORT_H */
