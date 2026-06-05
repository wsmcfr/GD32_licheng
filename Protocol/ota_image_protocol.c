#include "ota_image_protocol.h"

/*
 * 函数作用：
 *   从小端字节序缓冲区读取 32 位无符号整数。
 * 参数说明：
 *   data：指向至少 4 字节有效数据的缓冲区。
 * 返回值说明：
 *   返回解析出的 32 位数值；data 为空时返回 0。
 */
uint32_t ota_image_read_u32_le(const uint8_t *data)
{
    if(NULL == data){
        return 0U;
    }

    return ((uint32_t)data[0]) |
           ((uint32_t)data[1] << 8U) |
           ((uint32_t)data[2] << 16U) |
           ((uint32_t)data[3] << 24U);
}

/*
 * 函数作用：
 *   在已有 CRC32 中间值基础上继续追加一段数据。
 * 参数说明：
 *   crc：当前未取反 CRC32 中间值。
 *   data：待追加数据起始地址；length 大于 0 时必须是有效指针。
 *   length：待追加字节数，单位为字节。
 * 返回值说明：
 *   返回更新后的未取反 CRC32 中间值；非法空指针输入会保持原 CRC 不变。
 */
uint32_t ota_image_crc32_update(uint32_t crc, const uint8_t *data, uint32_t length)
{
    uint32_t index;
    uint32_t bit_index;

    if((NULL == data) && (length > 0U)){
        return crc;
    }

    for(index = 0U; index < length; index++){
        crc ^= data[index];
        for(bit_index = 0U; bit_index < 8U; bit_index++){
            if(0U != (crc & 1U)){
                crc = (crc >> 1U) ^ 0xEDB88320UL;
            }else{
                crc >>= 1U;
            }
        }
    }

    return crc;
}

/*
 * 函数作用：
 *   计算一段 RAM 数据的标准 CRC32。
 * 参数说明：
 *   data：待计算数据起始地址；length 大于 0 时必须是有效指针。
 *   length：待计算字节数，单位为字节。
 * 返回值说明：
 *   返回标准 CRC32 值；非法空指针输入返回 0。
 */
uint32_t ota_image_crc32_calc(const uint8_t *data, uint32_t length)
{
    uint32_t crc;

    if((NULL == data) && (length > 0U)){
        return 0U;
    }

    crc = ota_image_crc32_update(0xFFFFFFFFUL, data, length);
    return crc ^ 0xFFFFFFFFUL;
}

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
uint8_t ota_image_parse_header(const uint8_t *header_buffer, ota_image_header_t *header)
{
    if((NULL == header_buffer) || (NULL == header)){
        return 0U;
    }

    header->magic = ota_image_read_u32_le(&header_buffer[0]);
    header->header_size = ota_image_read_u32_le(&header_buffer[4]);
    header->image_size = ota_image_read_u32_le(&header_buffer[8]);
    header->load_addr = ota_image_read_u32_le(&header_buffer[12]);
    header->version = ota_image_read_u32_le(&header_buffer[16]);
    header->image_crc32 = ota_image_read_u32_le(&header_buffer[20]);
    header->flags = ota_image_read_u32_le(&header_buffer[24]);
    header->header_crc32 = ota_image_read_u32_le(&header_buffer[28]);
    header->stack_addr = ota_image_read_u32_le(&header_buffer[32]);
    header->entry_addr = ota_image_read_u32_le(&header_buffer[36]);

    return 1U;
}

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
                                                   const ota_image_header_t *header)
{
    uint8_t header_for_crc[OTA_IMAGE_HEADER_SIZE];
    uint32_t calc_header_crc32;
    uint32_t app_region_end;

    if((NULL == header_buffer) || (NULL == header)){
        return BOOTLOADER_PORT_STATUS_BAD_PARAM;
    }

    memcpy(header_for_crc, header_buffer, sizeof(header_for_crc));
    /*
     * header_crc32 字段自身不参与头部 CRC 计算。
     * 打包工具和接收端都按该字段清零后的 64 字节头部计算，才能保持一致。
     */
    header_for_crc[28] = 0U;
    header_for_crc[29] = 0U;
    header_for_crc[30] = 0U;
    header_for_crc[31] = 0U;
    calc_header_crc32 = ota_image_crc32_calc(header_for_crc, sizeof(header_for_crc));

    if((OTA_IMAGE_MAGIC != header->magic) ||
       (OTA_IMAGE_HEADER_SIZE != header->header_size) ||
       (0U == header->image_size) ||
       (header->image_size > BOOTLOADER_PORT_DOWNLOAD_MAX_SIZE) ||
       (BOOT_APP_START_ADDRESS != header->load_addr) ||
       (OTA_IMAGE_FLAGS_NONE != header->flags) ||
       (calc_header_crc32 != header->header_crc32)){
        return BOOTLOADER_PORT_STATUS_BAD_PARAM;
    }

    if((header->stack_addr < 0x20000000UL) || (header->stack_addr >= 0x20030000UL)){
        return BOOTLOADER_PORT_STATUS_BAD_VECTOR;
    }

    app_region_end = BOOT_APP_START_ADDRESS + BOOTLOADER_PORT_APP_MAX_SIZE;
    if((0U == (header->entry_addr & 1UL)) ||
       ((header->entry_addr & ~1UL) < BOOT_APP_START_ADDRESS) ||
       ((header->entry_addr & ~1UL) >= app_region_end)){
        return BOOTLOADER_PORT_STATUS_BAD_VECTOR;
    }

    return BOOTLOADER_PORT_STATUS_OK;
}
