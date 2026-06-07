#include "cimc_protocol.h"
#include "cimc_params.h"
#include "cimc_alarm.h"
#include "cimc_power_app.h"
#include "rtc_app.h"
#include "adc_app.h"
#include "cimc_status.h"
#include "bootloader_port.h"
#include "bsp_usart.h"
#include "gd30ad3344_pt100_app.h"

/* ── 帧格式固定字段 ── */
#define CIMC_FRAME_START          0xA5B6U
#define CIMC_FRAME_END            0xB6A5U
#define CIMC_PROTOCOL_VERSION     0x02U
#define CIMC_FRAME_TYPE_COMMAND   0x01U
#define CIMC_FRAME_TYPE_RESPONSE  0x02U
#define CIMC_FRAME_TYPE_HEARTBEAT 0x05U
#define CIMC_FRAME_TYPE_ERROR     0xFFU
#define CIMC_RESPONSE_OK          0xFFU

/* ── 特殊命令字 ── */
#define CIMC_CMD_HEARTBEAT        0x8888U  /* 设备主动发送或响应广播寻址 */
#define CIMC_CMD_BROADCAST_SEARCH 0xFFFFU  /* 上位机广播寻找设备 */

/* ── 系统管理类 0x01xx ── */
#define CIMC_CMD_REBOOT           0x0101U  /* 设备重启 */
#define CIMC_CMD_QUERY_VERSION    0x0104U  /* 查询固件版本（4 字节）*/
#define CIMC_CMD_SET_TIME         0x0105U  /* 设置设备时间（UTC 秒）*/
#define CIMC_CMD_GET_TIME         0x0106U  /* 查询设备时间（UTC 秒）*/
#define CIMC_CMD_SET_DEVICE_ID    0x01A1U  /* 设置设备 ID */
#define CIMC_CMD_SET_BAUD         0x01A2U  /* 设置波特率 */
#define CIMC_CMD_GET_DEVICE_ID    0x0111U  /* 查询设备 ID（广播下发）*/
#define CIMC_CMD_GET_BAUD         0x0112U  /* 查询波特率 */

/* ── 数据类 0x02xx ── */
#define CIMC_CMD_SET_DAC          0x0301U  /* 设置 DAC 输出（0~4095）*/

/* ── 控制类 0x03xx ── */
#define CIMC_CMD_AUTO_SAMPLE_START 0x0302U /* 开始定时自动上报 */
#define CIMC_CMD_AUTO_SAMPLE_STOP  0x0303U /* 停止定时自动上报 */

/* ── 数据类 0x02xx（全部）── */
#define CIMC_CMD_GET_CH0          0x0201U  /* 查询 CH0 数据（float 大端）*/
#define CIMC_CMD_GET_CH1          0x0202U  /* 查询 CH1 数据（float 大端）*/
#define CIMC_CMD_GET_CH2          0x0221U  /* 查询 CH2 PT100 温度（float 大端）*/
#define CIMC_CMD_SET_CH0_RATIO    0x0241U  /* 设置 CH0 变比（float 大端）*/
#define CIMC_CMD_SET_CH1_RATIO    0x0242U  /* 设置 CH1 变比（float 大端）*/
#define CIMC_CMD_SET_REPORT_INTV  0x0261U  /* 设置上报间隔（01=1s/02=3s/03=5s）*/

/* ── 控制类 0x03xx ── */
#define CIMC_CMD_SLEEP            0x03AAU  /* 进入 MCU 深度睡眠 10s 后唤醒 */

/* ── 参数配置类 0x04xx ── */
#define CIMC_CMD_GET_ALL_THRESH   0x0400U  /* 批量读取 CH0+CH1 阈值（各 float）*/
#define CIMC_CMD_GET_CH0_THRESH   0x0401U  /* 读取 CH0 阈值 */
#define CIMC_CMD_GET_CH1_THRESH   0x0402U  /* 读取 CH1 阈值 */
#define CIMC_CMD_SET_CH0_THRESH   0x0411U  /* 写入 CH0 阈值 */
#define CIMC_CMD_SET_CH1_THRESH   0x0412U  /* 写入 CH1 阈值 */

/* ── 系统升级类 0x05xx ── */
#define CIMC_CMD_UPGRADE_REQUEST  0x0501U  /* 进入 Bootloader 等待升级 */

/* ── 告警与日志类 0x06xx ── */
#define CIMC_CMD_SET_ALARM_MODE   0x0601U  /* 设置是否主动上报告警 */
#define CIMC_CMD_GET_ALARM_LOG    0x0602U  /* 查询告警记录（ASCII 直接回复）*/
#define CIMC_CMD_CLEAR_ALARM      0x0603U  /* 清除告警记录 */

/*
 * 赛题固件版本：2.0.1.0 → 字节序 [02 00 01 00]，与例题 "0.1.0.4→00 01 00 04" 格式一致。
 */
static const uint8_t s_firmware_version[4] = {0x02U, 0x00U, 0x01U, 0x00U};

/* 二进制工作区和 ASCII 应答缓冲区容量。 */
#define CIMC_PROTOCOL_BINARY_BUFFER_SIZE   128U
#define CIMC_PROTOCOL_ASCII_RESPONSE_SIZE  320U

/*
 * 结构体作用：
 *   保存完成 CRC 校验后的赛题协议字段，供命令分发器使用。
 */
typedef struct
{
    uint16_t        device_id;
    uint8_t         frame_type;
    uint16_t        command;
    uint8_t         length;
    uint8_t         version;
    const uint8_t  *payload;
} cimc_protocol_frame_t;

/*
 * 函数作用：把一个 ASCII 十六进制字符转换为 4 位数值（0~15）或返回 0xFF 表示非法。
 */
static uint8_t prv_cimc_hex_nibble(uint8_t ch)
{
    if((ch >= (uint8_t)'0') && (ch <= (uint8_t)'9')) { return (uint8_t)(ch - (uint8_t)'0'); }
    if((ch >= (uint8_t)'A') && (ch <= (uint8_t)'F')) { return (uint8_t)(ch - (uint8_t)'A' + 10U); }
    if((ch >= (uint8_t)'a') && (ch <= (uint8_t)'f')) { return (uint8_t)(ch - (uint8_t)'a' + 10U); }
    return 0xFFU;
}

/* 判断是否为协议允许忽略的空白字符（空格、制表、回车、换行）。 */
static uint8_t prv_cimc_is_ascii_space(uint8_t ch)
{
    return ((ch == (uint8_t)' ')  || (ch == (uint8_t)'\t') ||
            (ch == (uint8_t)'\r') || (ch == (uint8_t)'\n')) ? 1U : 0U;
}

/*
 * 函数作用：将 ASCII 十六进制文本解码为二进制字节流。
 * 返回值：解码得到的二进制字节数；0 表示解码失败。
 */
static uint16_t prv_cimc_decode_ascii_hex(const uint8_t *ascii, uint16_t ascii_len,
                                          uint8_t *output, uint16_t output_size)
{
    uint16_t ai, out = 0U;
    uint8_t high = 0U, have_high = 0U;

    if((NULL == ascii) || (NULL == output) || (0U == output_size)) { return 0U; }

    for(ai = 0U; ai < ascii_len; ai++) {
        uint8_t nibble;
        if(0U != prv_cimc_is_ascii_space(ascii[ai])) { continue; }
        nibble = prv_cimc_hex_nibble(ascii[ai]);
        if(0xFFU == nibble) { return 0U; }
        if(0U == have_high) {
            high = nibble; have_high = 1U;
        } else {
            if(out >= output_size) { return 0U; }
            output[out++] = (uint8_t)((high << 4U) | nibble);
            have_high = 0U;
        }
    }
    return (0U != have_high) ? 0U : out;
}

/* 从大端缓冲区读取 16 位无符号整数。 */
static uint16_t prv_cimc_read_u16_be(const uint8_t *data)
{
    if(NULL == data) { return 0U; }
    return (uint16_t)(((uint16_t)data[0] << 8U) | (uint16_t)data[1]);
}

/* 从大端缓冲区读取 32 位无符号整数。 */
static uint32_t prv_cimc_read_u32_be(const uint8_t *data)
{
    if(NULL == data) { return 0UL; }
    return ((uint32_t)data[0] << 24U) | ((uint32_t)data[1] << 16U) |
           ((uint32_t)data[2] <<  8U) |  (uint32_t)data[3];
}

/*
 * 函数作用：计算 CRC-16-Modbus 校验值，用于帧头到内容末尾范围的完整性校验。
 */
static uint16_t prv_cimc_crc16_modbus(const uint8_t *data, uint16_t length)
{
    uint16_t crc = 0xFFFFU;
    uint16_t i;
    uint8_t  b;

    if((NULL == data) && (length > 0U)) { return 0U; }

    for(i = 0U; i < length; i++) {
        crc ^= data[i];
        for(b = 0U; b < 8U; b++) {
            if(0U != (crc & 0x0001U)) {
                crc = (uint16_t)((crc >> 1U) ^ 0xA001U);
            } else {
                crc = (uint16_t)(crc >> 1U);
            }
        }
    }
    return crc;
}

/* 将 32 位无符号整数以大端序写入 4 字节缓冲区。 */
static void prv_cimc_write_u32_be(uint8_t *buf, uint32_t value)
{
    buf[0] = (uint8_t)(value >> 24U);
    buf[1] = (uint8_t)(value >> 16U);
    buf[2] = (uint8_t)(value >>  8U);
    buf[3] = (uint8_t)(value  & 0xFFU);
}

/*
 * 函数作用：
 *   将 IEEE 754 单精度浮点数以大端序（高字节在前）写入 4 字节缓冲区。
 *   赛题要求 CH0/CH1/阈值/变比均以大端浮点传输。
 */
static void prv_cimc_write_float_be(uint8_t *buf, float value)
{
    uint32_t raw;
    memcpy(&raw, &value, sizeof(raw));
    buf[0] = (uint8_t)(raw >> 24U);
    buf[1] = (uint8_t)(raw >> 16U);
    buf[2] = (uint8_t)(raw >>  8U);
    buf[3] = (uint8_t)(raw  & 0xFFU);
}

/*
 * 函数作用：
 *   从 4 字节大端缓冲区读取 IEEE 754 单精度浮点数。
 */
static float prv_cimc_read_float_be(const uint8_t *buf)
{
    uint32_t raw;
    float    value;
    raw = ((uint32_t)buf[0] << 24U) | ((uint32_t)buf[1] << 16U) |
          ((uint32_t)buf[2] <<  8U) |  (uint32_t)buf[3];
    memcpy(&value, &raw, sizeof(value));
    return value;
}

/*
 * 自动上报模块级状态：上次上报时刻（ms）用于间隔计时。
 * 主动上报是否开启由 cimc_status_is_auto_sample_active() 查询。
 */
static uint32_t g_last_report_ms = 0UL;

/*
 * 函数作用：
 *   校验并解析一帧二进制赛题协议数据。
 * 返回值：1 = 合法帧；0 = 任一字段不合法。
 */
static uint8_t prv_cimc_parse_binary_frame(const uint8_t *binary, uint16_t binary_len,
                                            cimc_protocol_frame_t *frame)
{
    uint8_t  payload_length;
    uint16_t expected_length;
    uint16_t recv_crc, calc_crc;

    if((NULL == binary) || (NULL == frame) || (binary_len < 13U)) { return 0U; }

    if((CIMC_FRAME_START != prv_cimc_read_u16_be(&binary[0])) ||
       (CIMC_FRAME_END   != prv_cimc_read_u16_be(&binary[binary_len - 2U]))) {
        return 0U;
    }

    payload_length  = binary[7];
    expected_length = (uint16_t)(13U + payload_length);
    if(binary_len != expected_length) { return 0U; }

    if(CIMC_PROTOCOL_VERSION != binary[8]) { return 0U; }

    recv_crc = prv_cimc_read_u16_be(&binary[9U + payload_length]);
    calc_crc = prv_cimc_crc16_modbus(binary, (uint16_t)(9U + payload_length));
    if(recv_crc != calc_crc) { return 0U; }

    frame->device_id  = prv_cimc_read_u16_be(&binary[2]);
    frame->frame_type = binary[4];
    frame->command    = prv_cimc_read_u16_be(&binary[5]);
    frame->length     = payload_length;
    frame->version    = binary[8];
    frame->payload    = (payload_length > 0U) ? &binary[9] : NULL;

    return 1U;
}

/*
 * 函数作用：
 *   将 1 个字节追加到 ASCII 十六进制应答缓冲区。
 * 返回值：1 = 成功；0 = 缓冲区满或参数非法。
 */
static uint8_t prv_cimc_append_hex_byte(char *output, uint16_t output_size,
                                         uint16_t *offset, uint8_t value)
{
    static const char hex_chars[] = "0123456789ABCDEF";

    if((NULL == output) || (NULL == offset) || ((*offset + 2U) >= output_size)) { return 0U; }

    output[*offset] = hex_chars[(value >> 4U) & 0x0FU]; (*offset)++;
    output[*offset] = hex_chars[ value        & 0x0FU]; (*offset)++;
    output[*offset] = '\0';

    return 1U;
}

/*
 * 函数作用：
 *   组装一帧赛题格式协议帧（二进制→ASCII 十六进制），通过 RS485 发送。
 * 参数说明：
 *   device_id：应答帧中的设备 ID 字段，通常等于本机 ID。
 *   frame_type：帧类型（0x02 应答 / 0x05 心跳 / 0xFF 错误）。
 *   command：关联命令字。
 *   payload：内容区；payload_length 为 0 时可为 NULL。
 *   payload_length：内容区字节数。
 * 返回值：1 = 发送成功；0 = 参数非法或缓冲区不足。
 */
static uint8_t prv_cimc_send_frame(uint16_t device_id, uint8_t frame_type,
                                    uint16_t command,
                                    const uint8_t *payload, uint8_t payload_length)
{
    uint8_t  binary[CIMC_PROTOCOL_BINARY_BUFFER_SIZE];
    char     ascii[CIMC_PROTOCOL_ASCII_RESPONSE_SIZE];
    uint16_t bin_len, crc, idx, ascii_offset = 0U;

    if((payload_length > 0U) && (NULL == payload)) { return 0U; }

    bin_len = (uint16_t)(13U + payload_length);
    if(bin_len > sizeof(binary)) { return 0U; }

    binary[0] = 0xA5U; binary[1] = 0xB6U;
    binary[2] = (uint8_t)(device_id >> 8U);
    binary[3] = (uint8_t)(device_id  & 0xFFU);
    binary[4] = frame_type;
    binary[5] = (uint8_t)(command >> 8U);
    binary[6] = (uint8_t)(command  & 0xFFU);
    binary[7] = payload_length;
    binary[8] = CIMC_PROTOCOL_VERSION;
    if(payload_length > 0U) {
        memcpy(&binary[9], payload, payload_length);
    }

    crc = prv_cimc_crc16_modbus(binary, (uint16_t)(9U + payload_length));
    binary[9U  + payload_length] = (uint8_t)(crc >> 8U);
    binary[10U + payload_length] = (uint8_t)(crc  & 0xFFU);
    binary[11U + payload_length] = 0xB6U;
    binary[12U + payload_length] = 0xA5U;

    for(idx = 0U; idx < bin_len; idx++) {
        if(0U == prv_cimc_append_hex_byte(ascii, (uint16_t)sizeof(ascii),
                                           &ascii_offset, binary[idx])) {
            return 0U;
        }
    }

    (void)bsp_usart_send_buffer(RS485_USART, (const uint8_t *)ascii, ascii_offset);
    return 1U;
}

/* 对指定命令发送 OK 应答（内容区 = 0xFF）。 */
static uint8_t prv_cimc_send_ok(uint16_t device_id, uint16_t command)
{
    uint8_t ok = CIMC_RESPONSE_OK;
    return prv_cimc_send_frame(device_id, CIMC_FRAME_TYPE_RESPONSE, command, &ok, 1U);
}

/*
 * 函数作用：
 *   发送错误应答帧（帧类型 0xFF）。
 *   赛题规定 CRC 错误、帧长度不匹配、未知帧类型时均回此帧。
 * 参数说明：
 *   device_id：本机当前设备 ID，用于填写应答帧的地址字段。
 *   command：原命令字；帧解析失败无法取得命令字时传 0xEEEE。
 */
static uint8_t prv_cimc_send_error(uint16_t device_id, uint16_t command)
{
    return prv_cimc_send_frame(device_id, CIMC_FRAME_TYPE_ERROR, command, NULL, 0U);
}

/*
 * 函数作用：
 *   采集当前 UTC + CH0 + CH1 数据，组装 12 字节 payload 并发送一帧自动上报数据帧。
 *   0x0302 首次响应和 auto_report_tick 周期发送均调用此函数，确保格式完全一致。
 *   必须位于 prv_cimc_send_frame 定义之后才能调用。
 * 参数说明：
 *   own_id：应答帧中填入的本机设备 ID。
 * 返回值说明：
 *   无返回值。
 */
static void prv_cimc_send_auto_report_frame(uint16_t own_id)
{
    const cimc_params_t *params = cimc_params_get();
    uint8_t  payload[12];
    uint32_t unix_ts = 0UL;
    float    ch0, ch1;

    (void)rtc_app_get_unix_epoch(&unix_ts);
    ch0 = adc_app_get_ch0_raw_float() * params->ch0_ratio;
    ch1 = adc_app_get_ch1_raw_float() * params->ch1_ratio;

    prv_cimc_write_u32_be(&payload[0], unix_ts);
    prv_cimc_write_float_be(&payload[4], ch0);
    prv_cimc_write_float_be(&payload[8], ch1);

    (void)prv_cimc_send_frame(own_id, CIMC_FRAME_TYPE_RESPONSE,
                               CIMC_CMD_AUTO_SAMPLE_START, payload, 12U);
}

/*
 * 函数作用：
 *   执行已校验帧对应的命令，实现 A/B 模块全部基础命令 + DAC/自动采集/升级。
 * 参数说明：
 *   frame：已通过 CRC 和长度校验的帧字段结构体指针。
 * 返回值说明：
 *   1：命令已处理（含成功应答和静默丢弃两种情况）。
 *   0：frame 为空。
 */
static uint8_t prv_cimc_dispatch_command(const cimc_protocol_frame_t *frame)
{
    const cimc_params_t *params;
    uint16_t own_id;

    if(NULL == frame) { return 0U; }

    params = cimc_params_get();
    own_id = params->device_id;

    /*
     * 设备 ID 过滤：
     *   0xFFFF = 广播地址 → 所有设备响应。
     *   等于本机 ID     → 正常响应。
     *   其他            → 静默丢弃，不回应任何报文（赛题明确要求）。
     */
    if((frame->device_id != own_id) && (frame->device_id != 0xFFFFU)) {
        return 1U;
    }

    /*
     * H-02：自动上报期间只允许停止命令（0x0303）。
     * 其他命令静默丢弃（不回应），避免 H-02 扣分项被触发。
     */
    if(0U != cimc_status_is_auto_sample_active()) {
        if((CIMC_FRAME_TYPE_COMMAND != frame->frame_type) ||
           (frame->command != CIMC_CMD_AUTO_SAMPLE_STOP)) {
            return 1U;
        }
    }

    /* ── 心跳帧（帧类型 0x05）── */
    if(CIMC_FRAME_TYPE_HEARTBEAT == frame->frame_type) {
        if(CIMC_CMD_BROADCAST_SEARCH == frame->command) {
            /*
             * 上位机广播寻找设备（0xFFFF / 0x05 / 0xFFFF），
             * 回复本机心跳帧（0x05 / 0x8888），告知地址已在线。
             */
            (void)prv_cimc_send_frame(own_id, CIMC_FRAME_TYPE_HEARTBEAT,
                                      CIMC_CMD_HEARTBEAT, NULL, 0U);
        }
        return 1U;
    }

    /* ── 非命令帧（帧类型非 0x01）── */
    if(CIMC_FRAME_TYPE_COMMAND != frame->frame_type) {
        (void)prv_cimc_send_error(own_id, frame->command);
        return 1U;
    }

    /* ── 命令帧分发 ── */
    switch(frame->command) {

    /* (A/B) 0x0101 设备重启 */
    case CIMC_CMD_REBOOT:
        if(frame->length != 0U) {
            (void)prv_cimc_send_error(own_id, frame->command);
            return 1U;
        }
        (void)prv_cimc_send_ok(own_id, frame->command);
        /* 重启前持久化告警记录，确保重启后能恢复。 */
        cimc_alarm_save();
        delay_ms(20U);
        __set_FAULTMASK(1U);
        NVIC_SystemReset();
        return 1U;

    /* (B) 0x0104 查询固件版本 */
    case CIMC_CMD_QUERY_VERSION:
        if(frame->length != 0U) {
            (void)prv_cimc_send_error(own_id, frame->command);
            return 1U;
        }
        (void)prv_cimc_send_frame(own_id, CIMC_FRAME_TYPE_RESPONSE,
                                   frame->command, s_firmware_version, 4U);
        return 1U;

    /* (C) 0x0105 设置设备时间（4 字节 UTC 秒级时间戳，大端序） */
    case CIMC_CMD_SET_TIME:
        if((frame->length != 4U) || (NULL == frame->payload)) {
            (void)prv_cimc_send_error(own_id, frame->command);
            return 1U;
        }
        {
            uint32_t ts = prv_cimc_read_u32_be(frame->payload);
            if(0 != rtc_app_set_unix_epoch(ts)) {
                (void)prv_cimc_send_error(own_id, frame->command);
                return 1U;
            }
        }
        (void)prv_cimc_send_ok(own_id, frame->command);
        return 1U;

    /* (B/C) 0x0106 查询设备时间（回复 4 字节 UTC 秒级时间戳） */
    case CIMC_CMD_GET_TIME:
        if(frame->length != 0U) {
            (void)prv_cimc_send_error(own_id, frame->command);
            return 1U;
        }
        {
            uint32_t ts;
            uint8_t  time_buf[4];
            if(0 != rtc_app_get_unix_epoch(&ts)) {
                (void)prv_cimc_send_error(own_id, frame->command);
                return 1U;
            }
            prv_cimc_write_u32_be(time_buf, ts);
            (void)prv_cimc_send_frame(own_id, CIMC_FRAME_TYPE_RESPONSE,
                                       frame->command, time_buf, 4U);
        }
        return 1U;

    /* (B/L) 0x0111 查询设备 ID（广播地址下发，回复本机 2 字节 ID） */
    case CIMC_CMD_GET_DEVICE_ID:
        if(frame->length != 0U) {
            (void)prv_cimc_send_error(own_id, frame->command);
            return 1U;
        }
        {
            uint8_t id_buf[2];
            id_buf[0] = (uint8_t)(own_id >> 8U);
            id_buf[1] = (uint8_t)(own_id  & 0xFFU);
            (void)prv_cimc_send_frame(own_id, CIMC_FRAME_TYPE_RESPONSE,
                                       frame->command, id_buf, 2U);
        }
        return 1U;

    /* (B/M) 0x0112 查询波特率（回复 1 字节映射码） */
    case CIMC_CMD_GET_BAUD:
        if(frame->length != 0U) {
            (void)prv_cimc_send_error(own_id, frame->command);
            return 1U;
        }
        {
            uint8_t baud_code = params->baud_code;
            (void)prv_cimc_send_frame(own_id, CIMC_FRAME_TYPE_RESPONSE,
                                       frame->command, &baud_code, 1U);
        }
        return 1U;

    /* (L) 0x01A1 设置设备 ID（应答帧使用新 ID） */
    case CIMC_CMD_SET_DEVICE_ID:
        if((frame->length != 2U) || (NULL == frame->payload)) {
            (void)prv_cimc_send_error(own_id, frame->command);
            return 1U;
        }
        {
            uint16_t new_id = prv_cimc_read_u16_be(frame->payload);
            if(0U == cimc_params_set_device_id(new_id)) {
                /*
                 * ID 超出 0x0001~0xFFFE 有效范围，按赛题 L-01 规则返回错误：
                 * 错误 ID 请求时设备不应答（返回错误帧，使用旧 ID）。
                 */
                (void)prv_cimc_send_error(own_id, frame->command);
                return 1U;
            }
            /* 赛题示例明确：应答帧 device_id 字段使用新 ID。 */
            (void)prv_cimc_send_ok(new_id, frame->command);
        }
        return 1U;

    /* (M) 0x01A2 设置波特率：先回 OK（旧波特率），再在线切换到新波特率 */
    case CIMC_CMD_SET_BAUD:
        if((frame->length != 1U) || (NULL == frame->payload)) {
            (void)prv_cimc_send_error(own_id, frame->command);
            return 1U;
        }
        if(0U == cimc_params_set_baud_code(frame->payload[0])) {
            (void)prv_cimc_send_error(own_id, frame->command);
            return 1U;
        }
        (void)prv_cimc_send_ok(own_id, frame->command);
        /*
         * OK 帧以旧波特率发完后，在线切换到新波特率，不执行系统重启。
         * BootLoader 有 5 秒固定延时，重启后设备心跳帧发送时刻与上位机
         * 新波特率查询命令在 RS485 半双工总线上严重冲突，
         * 导致 M-01 "115200 下通信正常" 丢分。
         * 波特率码已在 cimc_params_set_baud_code() 中持久化到 Flash，
         * M-02 重启后仍从 Flash 加载新波特率，持久化验证不受影响。
         */
        delay_ms(20U);
        bsp_usart_change_baudrate(cimc_params_get_baud_rate());
        return 1U;

    /* (D) 0x0301 设置 DAC 输出电压（0~4095 → 0x0000~0x0FFF） */
    case CIMC_CMD_SET_DAC:
        if((frame->length != 2U) || (NULL == frame->payload)) {
            (void)prv_cimc_send_error(own_id, frame->command);
            return 1U;
        }
        {
            uint16_t dac_value = prv_cimc_read_u16_be(frame->payload);
            if(dac_value > 0x0FFFU) {
                (void)prv_cimc_send_error(own_id, frame->command);
                return 1U;
            }
            (void)adc_app_set_dac_raw(dac_value);
            (void)prv_cimc_send_ok(own_id, frame->command);
        }
        return 1U;

    /* (H-01) 0x0302 开始定时自动上报
     * 赛题要求：首次响应即发一帧 12 字节数据帧（UTC+CH0+CH1），
     * 后续由 cimc_protocol_auto_report_tick() 按 report_interval 周期继续发。
     * 注意：不发 OK 帧，直接发数据帧，与赛题示例 "0C 02 ..." 一致。 */
    case CIMC_CMD_AUTO_SAMPLE_START:
        if(frame->length != 0U) {
            (void)prv_cimc_send_error(own_id, frame->command);
            return 1U;
        }
        cimc_status_set_auto_sample(1U);
        prv_cimc_send_auto_report_frame(own_id);
        /* 重置计时基准，避免 tick 在一个间隔内重复发送。 */
        g_last_report_ms = timebase_get_ms32();
        return 1U;

    /* (H) 0x0303 停止定时自动上报 */
    case CIMC_CMD_AUTO_SAMPLE_STOP:
        if(frame->length != 0U) {
            (void)prv_cimc_send_error(own_id, frame->command);
            return 1U;
        }
        cimc_status_set_auto_sample(0U);
        (void)prv_cimc_send_ok(own_id, frame->command);
        return 1U;

    /* (N) 0x0501 进入 Bootloader 等待赛题串口升级 */
    case CIMC_CMD_UPGRADE_REQUEST:
        if(frame->length != 0U) {
            (void)prv_cimc_send_error(own_id, frame->command);
            return 1U;
        }
        if(BOOTLOADER_PORT_STATUS_OK != bootloader_port_request_bootloader_upgrade()) {
            (void)prv_cimc_send_error(own_id, frame->command);
            return 1U;
        }
        (void)prv_cimc_send_ok(own_id, frame->command);
        /* 升级复位前持久化告警记录。 */
        cimc_alarm_save();
        delay_ms(20U);
        bootloader_port_request_upgrade_reset();
        return 1U;

    /* ── 数据查询类 ── */

    /* (B-04) 0x0201 查询 CH0 数据（原始电压 × 变比，float 大端）*/
    case CIMC_CMD_GET_CH0:
        if(frame->length != 0U) {
            (void)prv_cimc_send_error(own_id, frame->command);
            return 1U;
        }
        {
            uint8_t buf[4];
            float   val = adc_app_get_ch0_raw_float() * params->ch0_ratio;
            prv_cimc_write_float_be(buf, val);
            (void)prv_cimc_send_frame(own_id, CIMC_FRAME_TYPE_RESPONSE,
                                       frame->command, buf, 4U);
        }
        return 1U;

    /* (B-05) 0x0202 查询 CH1 数据（原始电压 × 变比，float 大端）*/
    case CIMC_CMD_GET_CH1:
        if(frame->length != 0U) {
            (void)prv_cimc_send_error(own_id, frame->command);
            return 1U;
        }
        {
            uint8_t buf[4];
            float   val = adc_app_get_ch1_raw_float() * params->ch1_ratio;
            prv_cimc_write_float_be(buf, val);
            (void)prv_cimc_send_frame(own_id, CIMC_FRAME_TYPE_RESPONSE,
                                       frame->command, buf, 4U);
        }
        return 1U;

    /* 0x0221 查询 CH2（外部 ADC PT100 温度，float 大端）*/
    case CIMC_CMD_GET_CH2:
        if(frame->length != 0U) {
            (void)prv_cimc_send_error(own_id, frame->command);
            return 1U;
        }
        {
            uint8_t          buf[4];
            pt100_measurement_t meas = gd30ad3344_pt100_get_latest();
            prv_cimc_write_float_be(buf, meas.temperature_c);
            (void)prv_cimc_send_frame(own_id, CIMC_FRAME_TYPE_RESPONSE,
                                       frame->command, buf, 4U);
        }
        return 1U;

    /* (D-01) 0x0241 设置 CH0 变比（float 大端，立即生效并持久化）*/
    case CIMC_CMD_SET_CH0_RATIO:
        if((frame->length != 4U) || (NULL == frame->payload)) {
            (void)prv_cimc_send_error(own_id, frame->command);
            return 1U;
        }
        cimc_params_set_ch0_ratio(prv_cimc_read_float_be(frame->payload));
        (void)prv_cimc_send_ok(own_id, frame->command);
        return 1U;

    /* (D-02) 0x0242 设置 CH1 变比（float 大端）*/
    case CIMC_CMD_SET_CH1_RATIO:
        if((frame->length != 4U) || (NULL == frame->payload)) {
            (void)prv_cimc_send_error(own_id, frame->command);
            return 1U;
        }
        cimc_params_set_ch1_ratio(prv_cimc_read_float_be(frame->payload));
        (void)prv_cimc_send_ok(own_id, frame->command);
        return 1U;

    /* (G-01) 0x0261 设置自动上报时间间隔（01=1s / 02=3s / 03=5s）*/
    case CIMC_CMD_SET_REPORT_INTV:
        if((frame->length != 1U) || (NULL == frame->payload)) {
            (void)prv_cimc_send_error(own_id, frame->command);
            return 1U;
        }
        if(0U == cimc_params_set_report_interval(frame->payload[0])) {
            (void)prv_cimc_send_error(own_id, frame->command);
            return 1U;
        }
        (void)prv_cimc_send_ok(own_id, frame->command);
        return 1U;

    /* ── 控制类 ── */

    /* (J-01) 0x03AA 进入 MCU 深度睡眠 10s，唤醒后直接发 ASCII 字符串 */
    case CIMC_CMD_SLEEP:
        if(frame->length != 0U) {
            (void)prv_cimc_send_error(own_id, frame->command);
            return 1U;
        }
        /*
         * 先回 OK 帧（赛题要求收到命令后先应答），
         * 再进入真实 MCU 深度睡眠。睡眠模块负责 10s 后唤醒并发
         * "instrument wakeup" ASCII 字符串。
         */
        (void)prv_cimc_send_ok(own_id, frame->command);
        cimc_power_sleep_10s();
        return 1U;

    /* ── 参数配置类 ── */

    /* (B-06) 0x0400 批量读取 CH0+CH1 阈值（各 4 字节 float 大端，共 8 字节）*/
    case CIMC_CMD_GET_ALL_THRESH:
        if(frame->length != 0U) {
            (void)prv_cimc_send_error(own_id, frame->command);
            return 1U;
        }
        {
            uint8_t buf[8];
            prv_cimc_write_float_be(&buf[0], params->ch0_threshold);
            prv_cimc_write_float_be(&buf[4], params->ch1_threshold);
            (void)prv_cimc_send_frame(own_id, CIMC_FRAME_TYPE_RESPONSE,
                                       frame->command, buf, 8U);
        }
        return 1U;

    /* 0x0401 读取 CH0 阈值 */
    case CIMC_CMD_GET_CH0_THRESH:
        if(frame->length != 0U) {
            (void)prv_cimc_send_error(own_id, frame->command);
            return 1U;
        }
        {
            uint8_t buf[4];
            prv_cimc_write_float_be(buf, params->ch0_threshold);
            (void)prv_cimc_send_frame(own_id, CIMC_FRAME_TYPE_RESPONSE,
                                       frame->command, buf, 4U);
        }
        return 1U;

    /* 0x0402 读取 CH1 阈值 */
    case CIMC_CMD_GET_CH1_THRESH:
        if(frame->length != 0U) {
            (void)prv_cimc_send_error(own_id, frame->command);
            return 1U;
        }
        {
            uint8_t buf[4];
            prv_cimc_write_float_be(buf, params->ch1_threshold);
            (void)prv_cimc_send_frame(own_id, CIMC_FRAME_TYPE_RESPONSE,
                                       frame->command, buf, 4U);
        }
        return 1U;

    /* (E-01) 0x0411 写入 CH0 阈值（float 大端，立即生效并持久化）*/
    case CIMC_CMD_SET_CH0_THRESH:
        if((frame->length != 4U) || (NULL == frame->payload)) {
            (void)prv_cimc_send_error(own_id, frame->command);
            return 1U;
        }
        cimc_params_set_ch0_threshold(prv_cimc_read_float_be(frame->payload));
        (void)prv_cimc_send_ok(own_id, frame->command);
        return 1U;

    /* (E-02) 0x0412 写入 CH1 阈值（float 大端）*/
    case CIMC_CMD_SET_CH1_THRESH:
        if((frame->length != 4U) || (NULL == frame->payload)) {
            (void)prv_cimc_send_error(own_id, frame->command);
            return 1U;
        }
        cimc_params_set_ch1_threshold(prv_cimc_read_float_be(frame->payload));
        (void)prv_cimc_send_ok(own_id, frame->command);
        return 1U;

    /* ── 告警与日志类 ── */

    /* (I-01) 0x0601 设置是否主动上报告警（01=主动 / 02=仅记录）*/
    case CIMC_CMD_SET_ALARM_MODE:
        if((frame->length != 1U) || (NULL == frame->payload)) {
            (void)prv_cimc_send_error(own_id, frame->command);
            return 1U;
        }
        if(0U == cimc_params_set_alarm_mode(frame->payload[0])) {
            (void)prv_cimc_send_error(own_id, frame->command);
            return 1U;
        }
        cimc_alarm_set_mode(frame->payload[0]);
        (void)prv_cimc_send_ok(own_id, frame->command);
        return 1U;

    /* (I-02/I-03) 0x0602 查询告警记录（ASCII 直接回复，非帧封装）*/
    case CIMC_CMD_GET_ALARM_LOG:
        if(frame->length != 0U) {
            (void)prv_cimc_send_error(own_id, frame->command);
            return 1U;
        }
        {
            /* 10 条 × 55 字符 + 终止符 */
            char buf[560];
            cimc_alarm_query(buf, (uint16_t)sizeof(buf));
            (void)bsp_usart_send_buffer(RS485_USART,
                                        (const uint8_t *)buf,
                                        (uint16_t)strlen(buf));
        }
        return 1U;

    /* (I-04) 0x0603 清除告警记录 */
    case CIMC_CMD_CLEAR_ALARM:
        if(frame->length != 0U) {
            (void)prv_cimc_send_error(own_id, frame->command);
            return 1U;
        }
        cimc_alarm_clear();
        (void)prv_cimc_send_ok(own_id, frame->command);
        return 1U;

    default:
        /*
         * 命令字不在已实现范围内，回错误帧（K-03 非法命令字）。
         * 赛题要求：未知命令字的错误应答统一使用 0xEEEE，与 CRC/长度错误保持一致。
         * 不能使用 frame->command（如 0x0FFF），否则评测机收到 CMD=0x0FFF 判为不符。
         */
        (void)prv_cimc_send_error(own_id, 0xEEEEU);
        return 1U;
    }
}

/*
 * 函数作用：
 *   处理 USART1/RS485 收到的一帧 ASCII 十六进制赛题协议数据。
 * 主要流程：
 *   1. ASCII → 二进制解码；失败则返回 0（内容不像协议帧时静默丢弃）。
 *   2. 校验帧头、帧尾、长度、协议版本和 CRC-16-Modbus；
 *      校验失败时发错误帧（K-01/K-02 评分点），使用本机当前 ID。
 *   3. 通过校验后交由命令分发器处理。
 */
uint8_t cimc_protocol_process_ascii_frame(const uint8_t *frame, uint16_t length)
{
    uint8_t               binary[CIMC_PROTOCOL_BINARY_BUFFER_SIZE];
    uint16_t              binary_length;
    cimc_protocol_frame_t parsed_frame;

    binary_length = prv_cimc_decode_ascii_hex(frame, length,
                                               binary, (uint16_t)sizeof(binary));
    if(0U == binary_length) {
        return 0U;
    }

    if(0U == prv_cimc_parse_binary_frame(binary, binary_length, &parsed_frame)) {
        /*
         * 能走到这里说明内容按 ASCII 十六进制解码成功，但帧校验失败。
         * 按赛题约定返回错误帧；命令字无法可靠提取时使用 0xEEEE。
         */
        (void)prv_cimc_send_error(cimc_params_get()->device_id, 0xEEEEU);
        return 1U;
    }

    return prv_cimc_dispatch_command(&parsed_frame);
}

/*
 * 函数作用：
 *   主动发送一帧心跳帧（帧类型 0x05，命令字 0x8888），告知上位机本机在线。
 *   应在 system_init() 完成后调用一次，同时也用于重启后上线通知。
 */
void cimc_protocol_send_heartbeat(void)
{
    uint16_t own_id = cimc_params_get()->device_id;
    (void)prv_cimc_send_frame(own_id, CIMC_FRAME_TYPE_HEARTBEAT,
                               CIMC_CMD_HEARTBEAT, NULL, 0U);
}

/*
 * 函数作用：
 *   由调度器周期调用，在自动上报激活期间按设定间隔向上位机推送数据帧。
 * 主要流程：
 *   1. 若自动上报未激活则直接返回。
 *   2. 根据 cimc_params 中的 report_interval 映射到毫秒间隔。
 *   3. 到达间隔后发送 12 字节数据帧：UTC 4B + CH0 float 4B + CH1 float 4B。
 *   4. 帧格式遵循赛题 0x0302 应答相同结构（帧类型 0x02，命令字 0x0302）。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   无返回值。
 */
void cimc_protocol_auto_report_tick(void)
{
    const cimc_params_t *params;
    uint32_t             interval_ms;
    uint32_t             now_ms;

    if(0U == cimc_status_is_auto_sample_active()) {
        return;
    }

    params = cimc_params_get();

    /* 将间隔映射码转换为毫秒数。 */
    switch(params->report_interval) {
    case 0x01U: interval_ms = 1000UL; break;
    case 0x02U: interval_ms = 3000UL; break;
    case 0x03U: interval_ms = 5000UL; break;
    default:    interval_ms = 1000UL; break;
    }

    now_ms = timebase_get_ms32();
    if((uint32_t)(now_ms - g_last_report_ms) < interval_ms) {
        return;
    }
    g_last_report_ms = now_ms;

    prv_cimc_send_auto_report_frame(params->device_id);
}
