#ifndef BOOTLOADER_PORT_H
#define BOOTLOADER_PORT_H

/*
 * 文件作用：
 *   统一封装当前 App 与 BootLoader 交接时需要共享的地址常量、CRC 计算、
 *   下载缓存区擦写、参数区回写和软件复位接口。
 * 说明：
 *   该模块只负责“BootLoader 交接层”能力，不负责串口协议解析和会话状态机。
 */

#define SYSTEM_ALL_BASE_ONLY
#include "system_all.h"
#undef SYSTEM_ALL_BASE_ONLY

#ifdef __cplusplus
extern "C" {
#endif

/*
 * BootLoader 共享地址与容量常量。
 * 说明：
 *   内部 Flash 按题目要求规划为 BootLoader 64KB、参数区 4KB，
 *   以及运行区 / 备份区 / 缓存区三块 128KB：
 *   - 参数区：0x08010000 ~ 0x08010FFF。
 *   - 运行区：0x08011000 ~ 0x08030FFF。
 *   - 备份区：0x08031000 ~ 0x08050FFF。
 *   - 缓存区：0x08051000 ~ 0x08070FFF。
 *   0x08071000 ~ 0x0807FFFF 暂不使用，避免 OTA 搬运越过题目规定区域。
 */
#define BOOTLOADER_PORT_PARAM_ADDR         0x08010000UL
#define BOOTLOADER_PORT_BACKUP_ADDR        0x08031000UL
#define BOOTLOADER_PORT_DOWNLOAD_ADDR      0x08051000UL
#define BOOTLOADER_PORT_REGION_SIZE        (128U * 1024U)
#define BOOTLOADER_PORT_DOWNLOAD_MAX_SIZE  BOOTLOADER_PORT_REGION_SIZE
#define BOOTLOADER_PORT_PARAM_SIZE         (4U * 1024U)
#define BOOTLOADER_PORT_FLASH_PAGE_SIZE    4096U
#define BOOTLOADER_PORT_APP_MAX_SIZE       0x00020000UL
#define BOOTLOADER_PORT_MAGIC_WORD         0xC0DEF47AUL
#define BOOTLOADER_PORT_TAIL_MAGIC         0xA5A5C3C3UL
/* user_config 段容量，App 参数存储区不超过此值。 */
#define BOOTLOADER_PORT_USER_CONFIG_SIZE   512U

/*
 * 枚举作用：
 *   表示 BootLoader 交接层操作结果。
 * 成员说明：
 *   BOOTLOADER_PORT_STATUS_OK：操作成功。
 *   BOOTLOADER_PORT_STATUS_BAD_PARAM：输入参数非法或越界。
 *   BOOTLOADER_PORT_STATUS_BAD_VECTOR：固件向量表不符合 App 跳转要求。
 *   BOOTLOADER_PORT_STATUS_FLASH_ERROR：片内 Flash 擦写失败。
 */
typedef enum
{
    BOOTLOADER_PORT_STATUS_OK = 0,
    BOOTLOADER_PORT_STATUS_BAD_PARAM,
    BOOTLOADER_PORT_STATUS_BAD_VECTOR,
    BOOTLOADER_PORT_STATUS_FLASH_ERROR
} bootloader_port_status_t;

/*
 * 函数作用：
 *   计算一段 RAM 数据的 CRC32，供 OTA 头部、分包和整包校验复用。
 * 参数说明：
 *   data：待计算数据起始地址；当 length 大于 0 时必须是有效指针。
 *   length：待计算字节数，单位为字节。
 * 返回值说明：
 *   返回按 BootLoader 约定算法计算出的 CRC32 值。
 */
uint32_t bootloader_port_crc32_calc(const uint8_t *data, uint32_t length);

/*
 * 函数作用：
 *   在已有 CRC32 中间值基础上继续追加一段数据。
 * 参数说明：
 *   crc：未取反的 CRC32 中间值；新会话应传入 0xFFFFFFFF。
 *   data：待追加数据起始地址；当 length 大于 0 时必须是有效指针。
 *   length：待追加字节数，单位为字节。
 * 返回值说明：
 *   返回更新后的未取反 CRC32 中间值；最终结果仍需再异或 0xFFFFFFFF。
 */
uint32_t bootloader_port_crc32_update(uint32_t crc, const uint8_t *data, uint32_t length);

/*
 * 函数作用：
 *   校验固件镜像开头向量表是否满足 BootLoader 跳转要求。
 * 参数说明：
 *   firmware：固件镜像起始地址，也就是向量表第 0 项所在位置。
 *   firmware_size：固件长度，至少需要包含 8 字节向量表。
 *   stack_addr：可选输出参数；非空时返回解析出的 MSP 初值。
 *   entry_addr：可选输出参数；非空时返回解析出的 Reset_Handler 入口。
 * 返回值说明：
 *   BOOTLOADER_PORT_STATUS_OK：向量表合法。
 *   BOOTLOADER_PORT_STATUS_BAD_PARAM：指针为空或长度不足。
 *   BOOTLOADER_PORT_STATUS_BAD_VECTOR：MSP 或入口地址不在允许范围内。
 */
bootloader_port_status_t bootloader_port_validate_firmware_vector(const uint8_t *firmware,
                                                                  uint32_t firmware_size,
                                                                  uint32_t *stack_addr,
                                                                  uint32_t *entry_addr);

/*
 * 函数作用：
 *   擦除内部 Flash 下载缓存区，为接收新固件做准备。
 * 参数说明：
 *   firmware_size：本次准备接收的固件大小，单位为字节。
 * 返回值说明：
 *   BOOTLOADER_PORT_STATUS_OK：下载缓存区准备成功。
 *   BOOTLOADER_PORT_STATUS_BAD_PARAM：大小为 0 或超过下载缓存上限。
 *   BOOTLOADER_PORT_STATUS_FLASH_ERROR：任一页擦除失败。
 */
bootloader_port_status_t bootloader_port_prepare_download_area(uint32_t firmware_size);

/*
 * 函数作用：
 *   将一个 OTA 数据分包直接写入内部 Flash 下载缓存区。
 * 参数说明：
 *   offset：当前分包在完整固件中的字节偏移。
 *   data：当前分包数据起始地址。
 *   length：当前分包长度，单位为字节。
 * 返回值说明：
 *   BOOTLOADER_PORT_STATUS_OK：写入成功。
 *   BOOTLOADER_PORT_STATUS_BAD_PARAM：参数为空或写入范围越界。
 *   BOOTLOADER_PORT_STATUS_FLASH_ERROR：任一字节写入失败。
 */
bootloader_port_status_t bootloader_port_write_download_chunk(uint32_t offset,
                                                              const uint8_t *data,
                                                              uint32_t length);

/*
 * 函数作用：
 *   从下载缓存区回读指定长度内容，并按 BootLoader 算法重新计算 CRC32。
 * 参数说明：
 *   firmware_size：需要参与回读校验的固件长度，单位为字节。
 * 返回值说明：
 *   返回下载缓存区指定范围的 CRC32；若长度非法则返回 0。
 */
uint32_t bootloader_port_calc_download_crc32(uint32_t firmware_size);

/*
 * 函数作用：
 *   回写 BootLoader 参数区，通知 BootLoader 下次复位执行升级搬运。
 * 参数说明：
 *   app_version：本次升级固件版本号。
 *   firmware_size：固件长度，单位为字节。
 *   firmware_crc32：完整固件 CRC32。
 * 返回值说明：
 *   BOOTLOADER_PORT_STATUS_OK：参数区写入成功。
 *   BOOTLOADER_PORT_STATUS_BAD_PARAM：固件长度非法。
 *   BOOTLOADER_PORT_STATUS_FLASH_ERROR：参数区擦写失败。
 */
bootloader_port_status_t bootloader_port_write_upgrade_info(uint32_t app_version,
                                                            uint32_t firmware_size,
                                                            uint32_t firmware_crc32);

/*
 * 函数作用：
 *   写入”进入 Bootloader 等待串口升级”的请求标志。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   BOOTLOADER_PORT_STATUS_OK：参数区写入成功，复位后 Bootloader 将等待 0x0502/0x0503。
 *   BOOTLOADER_PORT_STATUS_FLASH_ERROR：参数区擦写失败。
 * 说明：
 *   该接口只用于 App 收到 0x0501 升级请求后触发 Bootloader 等待窗口；
 *   它不表示下载区已有有效固件，因此不能复用 updateStatus=0x01 的搬运语义。
 */
bootloader_port_status_t bootloader_port_request_bootloader_upgrade(void);

/*
 * 函数作用：
 *   触发 MCU 软件复位，让 BootLoader 读取参数区并接手升级搬运。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   无返回值；正常情况下不会返回。
 */
void bootloader_port_request_upgrade_reset(void);

/*
 * 函数作用：
 *   从参数区 user_config 段读取指定字节数到 RAM 缓冲区。
 *   Flash 为内存映射，读取无需解锁。
 * 参数说明：
 *   buf：接收数据的 RAM 缓冲区。
 *   size：读取字节数，不超过 BOOTLOADER_PORT_USER_CONFIG_SIZE。
 * 返回值说明：
 *   BOOTLOADER_PORT_STATUS_OK：读取成功。
 *   BOOTLOADER_PORT_STATUS_BAD_PARAM：buf 为空或 size 越界。
 */
bootloader_port_status_t bootloader_port_read_user_config(uint8_t *buf, uint16_t size);

/*
 * 函数作用：
 *   将 RAM 缓冲区中指定字节数写入参数区 user_config 段。
 *   内部执行整页（4KB）读-改-写，不影响参数区其他字段（如升级控制字段）。
 * 参数说明：
 *   buf：待写入数据的 RAM 缓冲区。
 *   size：写入字节数，不超过 BOOTLOADER_PORT_USER_CONFIG_SIZE。
 * 返回值说明：
 *   BOOTLOADER_PORT_STATUS_OK：写入成功。
 *   BOOTLOADER_PORT_STATUS_BAD_PARAM：buf 为空或 size 越界。
 *   BOOTLOADER_PORT_STATUS_FLASH_ERROR：Flash 擦写失败。
 */
bootloader_port_status_t bootloader_port_write_user_config(const uint8_t *buf, uint16_t size);

#ifdef __cplusplus
}
#endif

#endif /* BOOTLOADER_PORT_H */
