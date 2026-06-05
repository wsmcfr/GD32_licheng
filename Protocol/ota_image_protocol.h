#ifndef OTA_IMAGE_PROTOCOL_H
#define OTA_IMAGE_PROTOCOL_H

/*
 * 文件作用：
 *   定义“64 字节 OTA 头部 + 原始 App payload”协议层的字段、解析和校验接口。
 * 说明：
 *   Protocol 层只负责 OTA 镜像格式、字段合法性和 CRC32 校验，不负责串口接收、
 *   DMA 环形缓冲、Flash 擦写或 BootLoader 参数区回写。
 */

#define SYSTEM_ALL_BASE_ONLY
#include "system_all.h"
#undef SYSTEM_ALL_BASE_ONLY

#ifdef __cplusplus
extern "C" {
#endif

#define OTA_IMAGE_MAGIC              0x474F5441UL
#define OTA_IMAGE_HEADER_SIZE        64U
#define OTA_IMAGE_FLAGS_NONE         0U
#define OTA_IMAGE_VECTOR_BYTES       8U

/*
 * 结构体作用：
 *   保存 Project_ota.bin 固定 64 字节头部解析后的关键字段。
 * 成员说明：
 *   magic：固定魔数，用于判断发送文件是否为 OTA 镜像。
 *   header_size：头部长度，当前必须为 OTA_IMAGE_HEADER_SIZE。
 *   image_size：App payload 字节数，必须落在下载缓存区容量内。
 *   load_addr：payload 最终运行地址，必须等于 BOOT_APP_START_ADDRESS。
 *   version：升级版本号，后续写入 BootLoader 参数区。
 *   image_crc32：App payload 标准 CRC32。
 *   flags：预留标志位，当前必须为 OTA_IMAGE_FLAGS_NONE。
 *   header_crc32：头部标准 CRC32，计算时该字段自身清零。
 *   stack_addr：App 向量表第 0 项 MSP 初值。
 *   entry_addr：App 向量表第 1 项 Reset_Handler 入口。
 */
typedef struct
{
    uint32_t magic;
    uint32_t header_size;
    uint32_t image_size;
    uint32_t load_addr;
    uint32_t version;
    uint32_t image_crc32;
    uint32_t flags;
    uint32_t header_crc32;
    uint32_t stack_addr;
    uint32_t entry_addr;
} ota_image_header_t;

/*
 * 函数作用：
 *   从小端字节序缓冲区读取 32 位无符号整数。
 * 参数说明：
 *   data：指向至少 4 字节有效数据的缓冲区。
 * 返回值说明：
 *   返回解析出的 32 位数值；data 为空时返回 0。
 */
uint32_t ota_image_read_u32_le(const uint8_t *data);

/*
 * 函数作用：
 *   计算一段 RAM 数据的标准 CRC32。
 * 参数说明：
 *   data：待计算数据起始地址；length 大于 0 时必须是有效指针。
 *   length：待计算字节数，单位为字节。
 * 返回值说明：
 *   返回标准 CRC32 值；非法空指针输入返回 0。
 */
uint32_t ota_image_crc32_calc(const uint8_t *data, uint32_t length);

/*
 * 函数作用：
 *   在已有 CRC32 中间值基础上继续追加一段数据。
 * 参数说明：
 *   crc：未取反的 CRC32 中间值；新会话应传入 0xFFFFFFFF。
 *   data：待追加数据起始地址；length 大于 0 时必须是有效指针。
 *   length：待追加字节数，单位为字节。
 * 返回值说明：
 *   返回更新后的未取反 CRC32 中间值；最终结果需要调用者再异或 0xFFFFFFFF。
 */
uint32_t ota_image_crc32_update(uint32_t crc, const uint8_t *data, uint32_t length);

/*
 * 函数作用：
 *   把 64 字节 OTA 头部缓存解析到结构体。
 * 参数说明：
 *   header_buffer：指向完整 OTA_IMAGE_HEADER_SIZE 字节头部缓存。
 *   header：输出结构体，用于保存解析后的头部字段。
 * 返回值说明：
 *   1：解析成功。
 *   0：参数为空。
 */
uint8_t ota_image_parse_header(const uint8_t *header_buffer, ota_image_header_t *header);

/*
 * 函数作用：
 *   校验 OTA 头部字段、头部 CRC 和向量表元数据是否符合当前 Flash 分区契约。
 * 参数说明：
 *   header_buffer：原始 64 字节头部缓存，用于重新计算 header_crc32。
 *   header：已经解析出的 OTA 头部字段。
 * 返回值说明：
 *   BOOTLOADER_PORT_STATUS_OK：头部合法。
 *   BOOTLOADER_PORT_STATUS_BAD_PARAM：头部字段、大小、地址或 CRC 不合法。
 *   BOOTLOADER_PORT_STATUS_BAD_VECTOR：头部记录的 MSP 或入口地址不合法。
 */
bootloader_port_status_t ota_image_validate_header(const uint8_t *header_buffer,
                                                   const ota_image_header_t *header);

#ifdef __cplusplus
}
#endif

#endif /* OTA_IMAGE_PROTOCOL_H */
