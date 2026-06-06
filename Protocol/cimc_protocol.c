#include "cimc_protocol.h"
#include "adc_app.h"
#include "bootloader_port.h"
#include "cimc_status.h"
#include "bsp_usart.h"

/* 赛题协议固定标志、版本、帧类型和命令字定义。 */
#define CIMC_FRAME_START                    0xA5B6U
#define CIMC_FRAME_END                      0xB6A5U
#define CIMC_PROTOCOL_VERSION               0x02U
#define CIMC_FRAME_TYPE_COMMAND             0x01U
#define CIMC_FRAME_TYPE_RESPONSE            0x02U
#define CIMC_FRAME_TYPE_ERROR               0xFFU
#define CIMC_CMD_SET_DAC                    0x0301U
#define CIMC_CMD_AUTO_SAMPLE_START          0x0302U
#define CIMC_CMD_AUTO_SAMPLE_STOP           0x0303U
#define CIMC_CMD_UPGRADE_REQUEST            0x0501U
#define CIMC_RESPONSE_OK                    0xFFU
#define CIMC_DEVICE_ID_DEFAULT              0x0001U

/* 一帧赛题普通命令远小于 256 字节，保留 128 字节二进制工作区即可覆盖当前命令。 */
#define CIMC_PROTOCOL_BINARY_BUFFER_SIZE    128U
#define CIMC_PROTOCOL_ASCII_RESPONSE_SIZE   320U

/*
 * 结构体作用：
 *   保存完成 CRC 校验后的赛题协议字段。
 * 成员说明：
 *   device_id：目标设备地址，0xFFFF 为广播地址。
 *   frame_type：帧类型，当前 App 只处理 0x01 命令下发帧。
 *   command：命令字。
 *   length：内容长度，不包含帧头、CRC 和帧尾。
 *   version：协议版本，当前固定为 0x02。
 *   payload：内容区指针，指向调用方二进制帧缓冲区内部。
 */
typedef struct
{
    uint16_t device_id;
    uint8_t frame_type;
    uint16_t command;
    uint8_t length;
    uint8_t version;
    const uint8_t *payload;
} cimc_protocol_frame_t;

/*
 * 函数作用：
 *   把一个 ASCII 十六进制字符转换为 4 位数值。
 * 参数说明：
 *   ch：输入字符，允许 '0'~'9'、'A'~'F'、'a'~'f'。
 * 返回值说明：
 *   0~15：合法十六进制字符对应的数值。
 *   0xFF：非法字符。
 */
static uint8_t prv_cimc_hex_nibble(uint8_t ch)
{
    if((ch >= (uint8_t)'0') && (ch <= (uint8_t)'9')) {
        return (uint8_t)(ch - (uint8_t)'0');
    }

    if((ch >= (uint8_t)'A') && (ch <= (uint8_t)'F')) {
        return (uint8_t)(ch - (uint8_t)'A' + 10U);
    }

    if((ch >= (uint8_t)'a') && (ch <= (uint8_t)'f')) {
        return (uint8_t)(ch - (uint8_t)'a' + 10U);
    }

    return 0xFFU;
}

/*
 * 函数作用：
 *   判断输入字符是否是串口工具常见的分隔空白。
 * 参数说明：
 *   ch：待判断字符。
 * 返回值说明：
 *   1：表示空格、制表、回车或换行。
 *   0：表示需要按协议内容处理的字符。
 */
static uint8_t prv_cimc_is_ascii_space(uint8_t ch)
{
    return ((ch == (uint8_t)' ') ||
            (ch == (uint8_t)'\t') ||
            (ch == (uint8_t)'\r') ||
            (ch == (uint8_t)'\n')) ? 1U : 0U;
}

/*
 * 函数作用：
 *   将 USART 收到的 ASCII 十六进制文本解码为二进制协议帧。
 * 参数说明：
 *   ascii：原始 ASCII 缓冲区。
 *   ascii_length：原始 ASCII 有效长度，单位为字节。
 *   output：输出二进制缓冲区。
 *   output_size：输出缓冲区容量，单位为字节。
 * 返回值说明：
 *   大于 0：解码得到的二进制字节数。
 *   0：输入参数非法、字符非法、半字节不成对或输出容量不足。
 */
static uint16_t prv_cimc_decode_ascii_hex(const uint8_t *ascii,
                                          uint16_t ascii_length,
                                          uint8_t *output,
                                          uint16_t output_size)
{
    uint16_t ascii_index;
    uint16_t out_index = 0U;
    uint8_t high_nibble = 0U;
    uint8_t have_high = 0U;

    if((NULL == ascii) || (NULL == output) || (0U == output_size)) {
        return 0U;
    }

    for(ascii_index = 0U; ascii_index < ascii_length; ascii_index++) {
        uint8_t nibble;

        if(0U != prv_cimc_is_ascii_space(ascii[ascii_index])) {
            continue;
        }

        nibble = prv_cimc_hex_nibble(ascii[ascii_index]);
        if(0xFFU == nibble) {
            return 0U;
        }

        if(0U == have_high) {
            high_nibble = nibble;
            have_high = 1U;
        } else {
            if(out_index >= output_size) {
                return 0U;
            }
            output[out_index] = (uint8_t)((high_nibble << 4U) | nibble);
            out_index++;
            have_high = 0U;
        }
    }

    if(0U != have_high) {
        return 0U;
    }

    return out_index;
}

/*
 * 函数作用：
 *   从大端二进制字段读取 16 位无符号整数。
 * 参数说明：
 *   data：指向至少 2 字节有效数据的缓冲区。
 * 返回值说明：
 *   返回按大端序组合后的 16 位值；data 为空时返回 0。
 */
static uint16_t prv_cimc_read_u16_be(const uint8_t *data)
{
    if(NULL == data) {
        return 0U;
    }

    return (uint16_t)(((uint16_t)data[0] << 8U) | (uint16_t)data[1]);
}

/*
 * 函数作用：
 *   计算赛题要求的 CRC-16-Modbus 校验。
 * 参数说明：
 *   data：参与 CRC 的二进制数据起始地址。
 *   length：参与 CRC 的字节数。
 * 返回值说明：
 *   返回 CRC16 结果。发送时按赛题要求用大端序放入帧中。
 */
static uint16_t prv_cimc_crc16_modbus(const uint8_t *data, uint16_t length)
{
    uint16_t crc = 0xFFFFU;
    uint16_t index;
    uint8_t bit_index;

    if((NULL == data) && (length > 0U)) {
        return 0U;
    }

    for(index = 0U; index < length; index++) {
        crc ^= data[index];
        for(bit_index = 0U; bit_index < 8U; bit_index++) {
            if(0U != (crc & 0x0001U)) {
                crc = (uint16_t)((crc >> 1U) ^ 0xA001U);
            } else {
                crc = (uint16_t)(crc >> 1U);
            }
        }
    }

    return crc;
}

/*
 * 函数作用：
 *   校验并解析一帧二进制赛题协议数据。
 * 参数说明：
 *   binary：ASCII 解码后的二进制帧。
 *   binary_length：二进制帧长度，单位为字节。
 *   frame：解析输出结构体。
 * 返回值说明：
 *   1：帧头、帧尾、长度、协议版本和 CRC 均合法。
 *   0：任一字段非法。
 */
static uint8_t prv_cimc_parse_binary_frame(const uint8_t *binary,
                                           uint16_t binary_length,
                                           cimc_protocol_frame_t *frame)
{
    uint8_t payload_length;
    uint16_t expected_length;
    uint16_t received_crc;
    uint16_t calculated_crc;

    if((NULL == binary) || (NULL == frame) || (binary_length < 13U)) {
        return 0U;
    }

    if((CIMC_FRAME_START != prv_cimc_read_u16_be(&binary[0])) ||
       (CIMC_FRAME_END != prv_cimc_read_u16_be(&binary[binary_length - 2U]))) {
        return 0U;
    }

    payload_length = binary[7];
    expected_length = (uint16_t)(13U + payload_length);
    if(binary_length != expected_length) {
        return 0U;
    }

    if(CIMC_PROTOCOL_VERSION != binary[8]) {
        return 0U;
    }

    received_crc = prv_cimc_read_u16_be(&binary[9U + payload_length]);
    calculated_crc = prv_cimc_crc16_modbus(binary, (uint16_t)(9U + payload_length));
    if(received_crc != calculated_crc) {
        return 0U;
    }

    frame->device_id = prv_cimc_read_u16_be(&binary[2]);
    frame->frame_type = binary[4];
    frame->command = prv_cimc_read_u16_be(&binary[5]);
    frame->length = payload_length;
    frame->version = binary[8];
    frame->payload = (payload_length > 0U) ? &binary[9] : NULL;

    return 1U;
}

/*
 * 函数作用：
 *   将 1 个字节追加到 ASCII 十六进制应答缓冲区。
 * 参数说明：
 *   output：ASCII 输出缓冲区。
 *   output_size：输出缓冲区容量。
 *   offset：当前写入偏移，函数成功后会更新该值。
 *   value：待追加的二进制字节。
 * 返回值说明：
 *   1：追加成功。
 *   0：输出缓冲区空间不足或参数非法。
 */
static uint8_t prv_cimc_append_hex_byte(char *output,
                                        uint16_t output_size,
                                        uint16_t *offset,
                                        uint8_t value)
{
    static const char hex_chars[] = "0123456789ABCDEF";

    if((NULL == output) || (NULL == offset) || ((*offset + 2U) >= output_size)) {
        return 0U;
    }

    output[*offset] = hex_chars[(value >> 4U) & 0x0FU];
    (*offset)++;
    output[*offset] = hex_chars[value & 0x0FU];
    (*offset)++;
    output[*offset] = '\0';

    return 1U;
}

/*
 * 函数作用：
 *   发送一帧赛题格式应答或错误帧。
 * 参数说明：
 *   device_id：应答设备 ID，普通命令回原目标设备 ID，广播查询可回本机 ID。
 *   frame_type：应答帧类型，常用 0x02 或 0xFF。
 *   command：应答关联命令字。
 *   payload：应答内容区；长度为 0 时可为空。
 *   payload_length：应答内容字节数。
 * 返回值说明：
 *   1：组帧并调用 RS485 发送成功。
 *   0：参数非法或缓冲区不足。
 */
static uint8_t prv_cimc_send_frame(uint16_t device_id,
                                   uint8_t frame_type,
                                   uint16_t command,
                                   const uint8_t *payload,
                                   uint8_t payload_length)
{
    uint8_t binary[CIMC_PROTOCOL_BINARY_BUFFER_SIZE];
    char ascii[CIMC_PROTOCOL_ASCII_RESPONSE_SIZE];
    uint16_t binary_length;
    uint16_t crc;
    uint16_t index;
    uint16_t ascii_offset = 0U;

    if((payload_length > 0U) && (NULL == payload)) {
        return 0U;
    }

    binary_length = (uint16_t)(13U + payload_length);
    if(binary_length > sizeof(binary)) {
        return 0U;
    }

    binary[0] = 0xA5U;
    binary[1] = 0xB6U;
    binary[2] = (uint8_t)(device_id >> 8U);
    binary[3] = (uint8_t)(device_id & 0xFFU);
    binary[4] = frame_type;
    binary[5] = (uint8_t)(command >> 8U);
    binary[6] = (uint8_t)(command & 0xFFU);
    binary[7] = payload_length;
    binary[8] = CIMC_PROTOCOL_VERSION;
    if(payload_length > 0U) {
        memcpy(&binary[9], payload, payload_length);
    }

    crc = prv_cimc_crc16_modbus(binary, (uint16_t)(9U + payload_length));
    binary[9U + payload_length] = (uint8_t)(crc >> 8U);
    binary[10U + payload_length] = (uint8_t)(crc & 0xFFU);
    binary[11U + payload_length] = 0xB6U;
    binary[12U + payload_length] = 0xA5U;

    for(index = 0U; index < binary_length; index++) {
        if(0U == prv_cimc_append_hex_byte(ascii,
                                          (uint16_t)sizeof(ascii),
                                          &ascii_offset,
                                          binary[index])) {
            return 0U;
        }
    }

    (void)bsp_usart_send_buffer(RS485_USART, (const uint8_t *)ascii, ascii_offset);

    return 1U;
}

/*
 * 函数作用：
 *   对指定命令发送 OK 应答。
 * 参数说明：
 *   device_id：应答目标设备 ID。
 *   command：原命令字。
 * 返回值说明：
 *   1：应答已发送。
 *   0：发送失败。
 */
static uint8_t prv_cimc_send_ok(uint16_t device_id, uint16_t command)
{
    uint8_t ok = CIMC_RESPONSE_OK;

    return prv_cimc_send_frame(device_id,
                               CIMC_FRAME_TYPE_RESPONSE,
                               command,
                               &ok,
                               1U);
}

/*
 * 函数作用：
 *   发送赛题错误应答帧。
 * 参数说明：
 *   device_id：应答目标设备 ID。
 *   command：出错命令字；若帧未能解析到命令，可传 0xEEEE。
 * 返回值说明：
 *   1：错误帧已发送。
 *   0：发送失败。
 */
static uint8_t prv_cimc_send_error(uint16_t device_id, uint16_t command)
{
    return prv_cimc_send_frame(device_id,
                               CIMC_FRAME_TYPE_ERROR,
                               command,
                               NULL,
                               0U);
}

/*
 * 函数作用：
 *   执行已校验帧对应的最小正式命令集合。
 * 参数说明：
 *   frame：已通过 CRC 和长度校验的帧字段。
 * 返回值说明：
 *   1：命令已处理。
 *   0：该命令不属于当前最小接入范围。
 */
static uint8_t prv_cimc_dispatch_command(const cimc_protocol_frame_t *frame)
{
    uint16_t dac_value;

    if(NULL == frame) {
        return 0U;
    }

    if((frame->device_id != CIMC_DEVICE_ID_DEFAULT) && (frame->device_id != 0xFFFFU)) {
        /*
         * 不是本机 ID 且不是广播帧时静默丢弃，这是赛题修改设备 ID 流程的基础行为。
         */
        return 1U;
    }

    if(CIMC_FRAME_TYPE_COMMAND != frame->frame_type) {
        return prv_cimc_send_error(CIMC_DEVICE_ID_DEFAULT, frame->command);
    }

    switch(frame->command) {
    case CIMC_CMD_SET_DAC:
        if((frame->length != 2U) || (NULL == frame->payload)) {
            (void)prv_cimc_send_error(CIMC_DEVICE_ID_DEFAULT, frame->command);
            return 1U;
        }

        dac_value = prv_cimc_read_u16_be(frame->payload);
        if(dac_value > 0x0FFFU) {
            (void)prv_cimc_send_error(CIMC_DEVICE_ID_DEFAULT, frame->command);
            return 1U;
        }

        /*
         * DAC 由赛题 0x0301 命令独占控制，不能再被 ADC 直通逻辑覆盖。
         */
        (void)adc_app_set_dac_raw(dac_value);
        (void)prv_cimc_send_ok(CIMC_DEVICE_ID_DEFAULT, frame->command);
        return 1U;

    case CIMC_CMD_AUTO_SAMPLE_START:
        if(frame->length != 0U) {
            (void)prv_cimc_send_error(CIMC_DEVICE_ID_DEFAULT, frame->command);
            return 1U;
        }
        cimc_status_set_auto_sample(1U);
        (void)prv_cimc_send_ok(CIMC_DEVICE_ID_DEFAULT, frame->command);
        return 1U;

    case CIMC_CMD_AUTO_SAMPLE_STOP:
        if(frame->length != 0U) {
            (void)prv_cimc_send_error(CIMC_DEVICE_ID_DEFAULT, frame->command);
            return 1U;
        }
        cimc_status_set_auto_sample(0U);
        (void)prv_cimc_send_ok(CIMC_DEVICE_ID_DEFAULT, frame->command);
        return 1U;

    case CIMC_CMD_UPGRADE_REQUEST:
        if(frame->length != 0U) {
            (void)prv_cimc_send_error(CIMC_DEVICE_ID_DEFAULT, frame->command);
            return 1U;
        }
        if(BOOTLOADER_PORT_STATUS_OK != bootloader_port_request_bootloader_upgrade()) {
            (void)prv_cimc_send_error(CIMC_DEVICE_ID_DEFAULT, frame->command);
            return 1U;
        }
        (void)prv_cimc_send_ok(CIMC_DEVICE_ID_DEFAULT, frame->command);
        delay_ms(20U);
        bootloader_port_request_upgrade_reset();
        return 1U;

    default:
        (void)prv_cimc_send_error(CIMC_DEVICE_ID_DEFAULT, frame->command);
        return 1U;
    }
}

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
uint8_t cimc_protocol_process_ascii_frame(const uint8_t *frame, uint16_t length)
{
    uint8_t binary[CIMC_PROTOCOL_BINARY_BUFFER_SIZE];
    uint16_t binary_length;
    cimc_protocol_frame_t parsed_frame;

    binary_length = prv_cimc_decode_ascii_hex(frame,
                                              length,
                                              binary,
                                              (uint16_t)sizeof(binary));
    if(0U == binary_length) {
        return 0U;
    }

    if(0U == prv_cimc_parse_binary_frame(binary, binary_length, &parsed_frame)) {
        /*
         * 能走到这里说明收到的内容像一帧协议但校验失败。
         * 当前最小实现无法可靠取出原始命令字时，按赛题错误帧约定回复 0xEEEE。
         */
        (void)prv_cimc_send_error(CIMC_DEVICE_ID_DEFAULT, 0xEEEEU);
        return 1U;
    }

    return prv_cimc_dispatch_command(&parsed_frame);
}
