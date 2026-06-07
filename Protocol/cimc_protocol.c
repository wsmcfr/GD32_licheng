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
#define CIMC_CMD_HEARTBEAT        0x8888U
#define CIMC_CMD_BROADCAST_SEARCH 0xFFFFU

/* ── 系统管理类 0x01xx ── */
#define CIMC_CMD_REBOOT           0x0101U
#define CIMC_CMD_QUERY_VERSION    0x0104U
#define CIMC_CMD_SET_TIME         0x0105U
#define CIMC_CMD_GET_TIME         0x0106U
#define CIMC_CMD_SET_DEVICE_ID    0x01A1U
#define CIMC_CMD_SET_BAUD         0x01A2U
#define CIMC_CMD_GET_DEVICE_ID    0x0111U
#define CIMC_CMD_GET_BAUD         0x0112U

/* ── 数据类 0x02xx ── */
#define CIMC_CMD_GET_CH0          0x0201U
#define CIMC_CMD_GET_CH1          0x0202U
#define CIMC_CMD_GET_CH2          0x0221U
#define CIMC_CMD_SET_CH0_RATIO    0x0241U
#define CIMC_CMD_SET_CH1_RATIO    0x0242U
#define CIMC_CMD_SET_REPORT_INTV  0x0261U

/* ── 控制类 0x03xx ── */
#define CIMC_CMD_SET_DAC          0x0301U
#define CIMC_CMD_AUTO_SAMPLE_START 0x0302U
#define CIMC_CMD_AUTO_SAMPLE_STOP  0x0303U
#define CIMC_CMD_SLEEP            0x03AAU

/* ── 参数配置类 0x04xx ── */
#define CIMC_CMD_GET_ALL_THRESH   0x0400U
#define CIMC_CMD_GET_CH0_THRESH   0x0401U
#define CIMC_CMD_GET_CH1_THRESH   0x0402U
#define CIMC_CMD_SET_CH0_THRESH   0x0411U
#define CIMC_CMD_SET_CH1_THRESH   0x0412U

/* ── 升级类 0x05xx ── */
#define CIMC_CMD_UPGRADE_REQUEST  0x0501U

/* ── 告警日志类 0x06xx ── */
#define CIMC_CMD_SET_ALARM_MODE   0x0601U
#define CIMC_CMD_GET_ALARM_LOG    0x0602U
#define CIMC_CMD_CLEAR_ALARM      0x0603U

/* 固件版本：2.0.1.0 → [02 00 01 00] */
static const uint8_t s_fw_ver[4] = {0x02U, 0x00U, 0x01U, 0x00U};

#define BIN_BUF_SIZE   128
#define ASCII_BUF_SIZE 320

/* 存放校验通过后的帧字段，供分发器使用 */
typedef struct
{
    uint16_t        device_id;
    uint8_t         frame_type;
    uint16_t        command;
    uint8_t         length;
    uint8_t         version;
    const uint8_t  *payload;
} cimc_frame_t;

static uint32_t g_last_report_ms = 0; /* 上次自动上报时刻，用于间隔计时 */

/* ASCII十六进制字符转4位值，非法字符返回0xFF */
static uint8_t hex_nibble(uint8_t ch)
{
    if((ch >= (uint8_t)'0') && (ch <= (uint8_t)'9')) { return (uint8_t)(ch - (uint8_t)'0'); }
    if((ch >= (uint8_t)'A') && (ch <= (uint8_t)'F')) { return (uint8_t)(ch - (uint8_t)'A' + 10); }
    if((ch >= (uint8_t)'a') && (ch <= (uint8_t)'f')) { return (uint8_t)(ch - (uint8_t)'a' + 10); }
    return 0xFFU;
}

// 判断是否为协议允许忽略的空白字符（空格/制表/回车/换行）
static uint8_t is_space(uint8_t ch)
{
    return ((ch == (uint8_t)' ')  || (ch == (uint8_t)'\t') ||
            (ch == (uint8_t)'\r') || (ch == (uint8_t)'\n')) ? 1 : 0;
}

/* 将ASCII十六进制文本解码为二进制字节流，返回字节数，失败返回0 */
static uint16_t decode_ascii_hex(const uint8_t *ascii, uint16_t ascii_len,
                                 uint8_t *output, uint16_t output_size)
{
    uint16_t ai, out = 0;
    uint8_t high = 0, have_high = 0;

    if((NULL == ascii) || (NULL == output) || (0 == output_size)) { return 0; }

    for(ai = 0; ai < ascii_len; ai++) {
        uint8_t nibble;
        if(0 != is_space(ascii[ai])) { continue; }
        nibble = hex_nibble(ascii[ai]);
        if(0xFFU == nibble) { return 0; }
        if(0 == have_high) {
            high = nibble; have_high = 1;
        } else {
            if(out >= output_size) { return 0; }
            output[out++] = (uint8_t)((high << 4) | nibble);
            have_high = 0;
        }
    }
    return (0 != have_high) ? 0 : out;
}

// 从大端缓冲区读取16位无符号整数
static uint16_t read_u16_be(const uint8_t *data)
{
    if(NULL == data) { return 0; }
    return (uint16_t)(((uint16_t)data[0] << 8) | (uint16_t)data[1]);
}

// 从大端缓冲区读取32位无符号整数
static uint32_t read_u32_be(const uint8_t *data)
{
    if(NULL == data) { return 0; }
    return ((uint32_t)data[0] << 24) | ((uint32_t)data[1] << 16) |
           ((uint32_t)data[2] <<  8) |  (uint32_t)data[3];
}

/* CRC-16-Modbus校验，覆盖帧头到内容末尾 */
static uint16_t crc16_modbus(const uint8_t *data, uint16_t length)
{
    uint16_t crc = 0xFFFFU;
    uint16_t i;
    uint8_t  b;

    if((NULL == data) && (length > 0)) { return 0; }

    for(i = 0; i < length; i++) {
        crc ^= data[i];
        for(b = 0; b < 8; b++) {
            if(0 != (crc & 0x0001U)) {
                crc = (uint16_t)((crc >> 1) ^ 0xA001U);
            } else {
                crc = (uint16_t)(crc >> 1);
            }
        }
    }
    return crc;
}

// 将32位值以大端序写入4字节缓冲区
static void write_u32_be(uint8_t *buf, uint32_t value)
{
    buf[0] = (uint8_t)(value >> 24);
    buf[1] = (uint8_t)(value >> 16);
    buf[2] = (uint8_t)(value >>  8);
    buf[3] = (uint8_t)(value  & 0xFFU);
}

/* 将IEEE 754单精度浮点以大端序写入4字节，CH0/CH1/阈值/变比均用此格式 */
static void write_float_be(uint8_t *buf, float value)
{
    uint32_t raw;
    memcpy(&raw, &value, sizeof(raw));
    buf[0] = (uint8_t)(raw >> 24);
    buf[1] = (uint8_t)(raw >> 16);
    buf[2] = (uint8_t)(raw >>  8);
    buf[3] = (uint8_t)(raw  & 0xFFU);
}

// 从4字节大端缓冲区读取IEEE 754单精度浮点
static float read_float_be(const uint8_t *buf)
{
    uint32_t raw;
    float    value;
    raw = ((uint32_t)buf[0] << 24) | ((uint32_t)buf[1] << 16) |
          ((uint32_t)buf[2] <<  8) |  (uint32_t)buf[3];
    memcpy(&value, &raw, sizeof(value));
    return value;
}

/* 将1字节追加到ASCII十六进制输出缓冲区，缓冲区满返回0 */
static uint8_t append_hex_byte(char *output, uint16_t output_size,
                                uint16_t *offset, uint8_t value)
{
    static const char hex_chars[] = "0123456789ABCDEF";

    if((NULL == output) || (NULL == offset) || ((*offset + 2) >= output_size)) { return 0; }

    output[*offset] = hex_chars[(value >> 4) & 0x0FU]; (*offset)++;
    output[*offset] = hex_chars[ value        & 0x0FU]; (*offset)++;
    output[*offset] = '\0';

    return 1;
}

/*
 * 组装协议帧（二进制→ASCII十六进制）并通过RS485发送。
 * payload为NULL时payload_length须为0；帧格式：帧头+设备ID+帧类型+命令+长度+版本+内容+CRC+帧尾。
 */
static uint8_t send_frame(uint16_t device_id, uint8_t frame_type,
                          uint16_t command,
                          const uint8_t *payload, uint8_t payload_length)
{
    uint8_t  binary[BIN_BUF_SIZE];
    char     ascii[ASCII_BUF_SIZE];
    uint16_t bin_len, crc, idx, ascii_offset = 0;

    if((payload_length > 0) && (NULL == payload)) { return 0; }

    bin_len = (uint16_t)(13 + payload_length);
    if(bin_len > sizeof(binary)) { return 0; }

    binary[0] = 0xA5U; binary[1] = 0xB6U;
    binary[2] = (uint8_t)(device_id >> 8);
    binary[3] = (uint8_t)(device_id  & 0xFFU);
    binary[4] = frame_type;
    binary[5] = (uint8_t)(command >> 8);
    binary[6] = (uint8_t)(command  & 0xFFU);
    binary[7] = payload_length;
    binary[8] = CIMC_PROTOCOL_VERSION;
    if(payload_length > 0) {
        memcpy(&binary[9], payload, payload_length);
    }

    crc = crc16_modbus(binary, (uint16_t)(9 + payload_length));
    binary[9  + payload_length] = (uint8_t)(crc >> 8);
    binary[10 + payload_length] = (uint8_t)(crc  & 0xFFU);
    binary[11 + payload_length] = 0xB6U;
    binary[12 + payload_length] = 0xA5U;

    for(idx = 0; idx < bin_len; idx++) {
        if(0 == append_hex_byte(ascii, (uint16_t)sizeof(ascii),
                                  &ascii_offset, binary[idx])) {
            return 0;
        }
    }

    bsp_usart_send_buffer(RS485_USART, (const uint8_t *)ascii, ascii_offset);
    return 1;
}

/* 发送OK应答帧（内容区=0xFF） */
static uint8_t send_ok(uint16_t device_id, uint16_t command)
{
    uint8_t ok = CIMC_RESPONSE_OK;
    return send_frame(device_id, CIMC_FRAME_TYPE_RESPONSE, command, &ok, 1);
}

/* 发送错误应答帧（帧类型0xFF），CRC/长度/非法帧类型时回复 */
static uint8_t send_error(uint16_t device_id, uint16_t command)
{
    return send_frame(device_id, CIMC_FRAME_TYPE_ERROR, command, NULL, 0);
}

/* 采集UTC+CH0+CH1组装12字节payload，发送一帧自动上报数据帧 */
static void send_auto_report(uint16_t own_id)
{
    const cimc_params_t *params = cimc_params_get();
    uint8_t  payload[12];
    uint32_t unix_ts = 0;
    float    ch0, ch1;

    rtc_app_get_unix_epoch(&unix_ts);
    ch0 = adc_app_get_ch0_raw_float() * params->ch0_ratio;
    ch1 = adc_app_get_ch1_raw_float() * params->ch1_ratio;

    write_u32_be(&payload[0], unix_ts);
    write_float_be(&payload[4], ch0);
    write_float_be(&payload[8], ch1);

    send_frame(own_id, CIMC_FRAME_TYPE_RESPONSE,
                     CIMC_CMD_AUTO_SAMPLE_START, payload, 12);
}

/* 校验并解析一帧二进制协议数据，合法返回1，否则返回0 */
static uint8_t parse_frame(const uint8_t *binary, uint16_t binary_len,
                            cimc_frame_t *frame)
{
    uint8_t  payload_length;
    uint16_t expected_length;
    uint16_t recv_crc, calc_crc;

    if((NULL == binary) || (NULL == frame) || (binary_len < 13)) { return 0; }

    if((CIMC_FRAME_START != read_u16_be(&binary[0])) ||
       (CIMC_FRAME_END   != read_u16_be(&binary[binary_len - 2]))) {
        return 0;
    }

    payload_length  = binary[7];
    expected_length = (uint16_t)(13 + payload_length);
    if(binary_len != expected_length) { return 0; }

    if(CIMC_PROTOCOL_VERSION != binary[8]) { return 0; }

    recv_crc = read_u16_be(&binary[9 + payload_length]);
    calc_crc = crc16_modbus(binary, (uint16_t)(9 + payload_length));
    if(recv_crc != calc_crc) { return 0; }

    frame->device_id  = read_u16_be(&binary[2]);
    frame->frame_type = binary[4];
    frame->command    = read_u16_be(&binary[5]);
    frame->length     = payload_length;
    frame->version    = binary[8];
    frame->payload    = (payload_length > 0) ? &binary[9] : NULL;

    return 1;
}

/*
 * 执行已校验帧对应的命令。
 * 设备ID过滤：0xFFFF=广播，等于本机ID=正常响应，其他静默丢弃。
 * 自动上报激活期间只允许停止命令（0x0303），其他命令静默丢弃。
 */
static uint8_t dispatch_command(const cimc_frame_t *frame)
{
    const cimc_params_t *params;
    uint16_t own_id;

    if(NULL == frame) { return 0; }

    params = cimc_params_get();
    own_id = params->device_id;

    if((frame->device_id != own_id) && (frame->device_id != 0xFFFFU)) {
        return 1;
    }

    if(0 != cimc_status_is_auto_sample_active()) {
        if((CIMC_FRAME_TYPE_COMMAND != frame->frame_type) ||
           (frame->command != CIMC_CMD_AUTO_SAMPLE_STOP)) {
            return 1;
        }
    }

    /* 心跳帧（类型0x05）：广播寻址时回复本机心跳 */
    if(CIMC_FRAME_TYPE_HEARTBEAT == frame->frame_type) {
        if(CIMC_CMD_BROADCAST_SEARCH == frame->command) {
            send_frame(own_id, CIMC_FRAME_TYPE_HEARTBEAT,
                             CIMC_CMD_HEARTBEAT, NULL, 0);
        }
        return 1;
    }

    /* 非命令帧（类型非0x01）→ 回错误帧 */
    if(CIMC_FRAME_TYPE_COMMAND != frame->frame_type) {
        send_error(own_id, frame->command);
        return 1;
    }

    switch(frame->command) {

    case CIMC_CMD_REBOOT:  /* 0x0101 设备重启 */
        if(frame->length != 0) { send_error(own_id, frame->command); return 1; }
        send_ok(own_id, frame->command);
        cimc_alarm_save();
        delay_ms(20);
        __set_FAULTMASK(1);
        NVIC_SystemReset();
        return 1;

    case CIMC_CMD_QUERY_VERSION:  /* 0x0104 查询固件版本 */
        if(frame->length != 0) { send_error(own_id, frame->command); return 1; }
        send_frame(own_id, CIMC_FRAME_TYPE_RESPONSE,
                         frame->command, s_fw_ver, 4);
        return 1;

    case CIMC_CMD_SET_TIME:  /* 0x0105 设置设备时间（4字节UTC秒，大端） */
        if((frame->length != 4) || (NULL == frame->payload)) {
            send_error(own_id, frame->command); return 1;
        }
        {
            uint32_t ts = read_u32_be(frame->payload);
            if(0 != rtc_app_set_unix_epoch(ts)) {
                send_error(own_id, frame->command); return 1;
            }
        }
        send_ok(own_id, frame->command);
        return 1;

    case CIMC_CMD_GET_TIME:  /* 0x0106 查询设备时间（回4字节UTC秒） */
        if(frame->length != 0) { send_error(own_id, frame->command); return 1; }
        {
            uint32_t ts;
            uint8_t  buf[4];
            if(0 != rtc_app_get_unix_epoch(&ts)) {
                send_error(own_id, frame->command); return 1;
            }
            write_u32_be(buf, ts);
            send_frame(own_id, CIMC_FRAME_TYPE_RESPONSE, frame->command, buf, 4);
        }
        return 1;

    case CIMC_CMD_GET_DEVICE_ID:  /* 0x0111 查询设备ID（广播下发，回本机2字节ID） */
        if(frame->length != 0) { send_error(own_id, frame->command); return 1; }
        {
            uint8_t buf[2];
            buf[0] = (uint8_t)(own_id >> 8);
            buf[1] = (uint8_t)(own_id  & 0xFFU);
            send_frame(own_id, CIMC_FRAME_TYPE_RESPONSE, frame->command, buf, 2);
        }
        return 1;

    case CIMC_CMD_GET_BAUD:  /* 0x0112 查询波特率（回1字节映射码） */
        if(frame->length != 0) { send_error(own_id, frame->command); return 1; }
        {
            uint8_t baud_code = params->baud_code;
            send_frame(own_id, CIMC_FRAME_TYPE_RESPONSE, frame->command, &baud_code, 1);
        }
        return 1;

    case CIMC_CMD_SET_DEVICE_ID:  /* 0x01A1 设置设备ID（应答帧用新ID） */
        if((frame->length != 2) || (NULL == frame->payload)) {
            send_error(own_id, frame->command); return 1;
        }
        {
            uint16_t new_id = read_u16_be(frame->payload);
            if(0 == cimc_params_set_device_id(new_id)) {
                send_error(own_id, frame->command); return 1;
            }
            send_ok(new_id, frame->command);
        }
        return 1;

    case CIMC_CMD_SET_BAUD:  /* 0x01A2 设置波特率：先回OK（旧波特率），再在线切换 */
        if((frame->length != 1) || (NULL == frame->payload)) {
            send_error(own_id, frame->command); return 1;
        }
        if(0 == cimc_params_set_baud_code(frame->payload[0])) {
            send_error(own_id, frame->command); return 1;
        }
        send_ok(own_id, frame->command);
        /*
         * OK帧以旧波特率发完后，在线切换到新波特率，不重启。
         * 波特率码已由cimc_params_set_baud_code()持久化到Flash。
         */
        delay_ms(20);
        bsp_usart_change_baudrate(cimc_params_get_baud_rate());
        return 1;

    case CIMC_CMD_SET_DAC:  /* 0x0301 设置DAC输出（0~4095） */
        if((frame->length != 2) || (NULL == frame->payload)) {
            send_error(own_id, frame->command); return 1;
        }
        {
            uint16_t dac_val = read_u16_be(frame->payload);
            if(dac_val > 0x0FFFU) { send_error(own_id, frame->command); return 1; }
            adc_app_set_dac_raw(dac_val);
            send_ok(own_id, frame->command);
        }
        return 1;

    case CIMC_CMD_AUTO_SAMPLE_START:  /* 0x0302 开始定时自动上报，首次响应直接发数据帧 */
        if(frame->length != 0) { send_error(own_id, frame->command); return 1; }
        cimc_status_set_auto_sample(1);
        send_auto_report(own_id);
        g_last_report_ms = timebase_get_ms32();
        return 1;

    case CIMC_CMD_AUTO_SAMPLE_STOP:  /* 0x0303 停止定时自动上报 */
        if(frame->length != 0) { send_error(own_id, frame->command); return 1; }
        cimc_status_set_auto_sample(0);
        send_ok(own_id, frame->command);
        return 1;

    case CIMC_CMD_UPGRADE_REQUEST:  /* 0x0501 进入Bootloader等待升级 */
        if(frame->length != 0) { send_error(own_id, frame->command); return 1; }
        if(BOOTLOADER_PORT_STATUS_OK != bootloader_port_request_bootloader_upgrade()) {
            send_error(own_id, frame->command); return 1;
        }
        send_ok(own_id, frame->command);
        cimc_alarm_save();
        delay_ms(20);
        bootloader_port_request_upgrade_reset();
        return 1;

    case CIMC_CMD_GET_CH0:  /* 0x0201 查询CH0（原始电压×变比，float大端） */
        if(frame->length != 0) { send_error(own_id, frame->command); return 1; }
        {
            uint8_t buf[4];
            float   val = adc_app_get_ch0_raw_float() * params->ch0_ratio;
            write_float_be(buf, val);
            send_frame(own_id, CIMC_FRAME_TYPE_RESPONSE, frame->command, buf, 4);
        }
        return 1;

    case CIMC_CMD_GET_CH1:  /* 0x0202 查询CH1（原始电压×变比，float大端） */
        if(frame->length != 0) { send_error(own_id, frame->command); return 1; }
        {
            uint8_t buf[4];
            float   val = adc_app_get_ch1_raw_float() * params->ch1_ratio;
            write_float_be(buf, val);
            send_frame(own_id, CIMC_FRAME_TYPE_RESPONSE, frame->command, buf, 4);
        }
        return 1;

    case CIMC_CMD_GET_CH2:  /* 0x0221 查询CH2（PT100温度，float大端） */
        if(frame->length != 0) { send_error(own_id, frame->command); return 1; }
        {
            uint8_t          buf[4];
            pt100_measurement_t meas = gd30ad3344_pt100_get_latest();
            write_float_be(buf, meas.temperature_c);
            send_frame(own_id, CIMC_FRAME_TYPE_RESPONSE, frame->command, buf, 4);
        }
        return 1;

    case CIMC_CMD_SET_CH0_RATIO:  /* 0x0241 设置CH0变比 */
        if((frame->length != 4) || (NULL == frame->payload)) {
            send_error(own_id, frame->command); return 1;
        }
        cimc_params_set_ch0_ratio(read_float_be(frame->payload));
        send_ok(own_id, frame->command);
        return 1;

    case CIMC_CMD_SET_CH1_RATIO:  /* 0x0242 设置CH1变比 */
        if((frame->length != 4) || (NULL == frame->payload)) {
            send_error(own_id, frame->command); return 1;
        }
        cimc_params_set_ch1_ratio(read_float_be(frame->payload));
        send_ok(own_id, frame->command);
        return 1;

    case CIMC_CMD_SET_REPORT_INTV:  /* 0x0261 设置自动上报间隔（01=1s/02=3s/03=5s） */
        if((frame->length != 1) || (NULL == frame->payload)) {
            send_error(own_id, frame->command); return 1;
        }
        if(0 == cimc_params_set_report_interval(frame->payload[0])) {
            send_error(own_id, frame->command); return 1;
        }
        send_ok(own_id, frame->command);
        return 1;

    case CIMC_CMD_SLEEP:  /* 0x03AA 进入MCU深度睡眠10s后唤醒 */
        if(frame->length != 0) { send_error(own_id, frame->command); return 1; }
        send_ok(own_id, frame->command);
        cimc_power_sleep_10s();
        return 1;

    case CIMC_CMD_GET_ALL_THRESH:  /* 0x0400 批量读取CH0+CH1阈值（各float大端，共8字节） */
        if(frame->length != 0) { send_error(own_id, frame->command); return 1; }
        {
            uint8_t buf[8];
            write_float_be(&buf[0], params->ch0_threshold);
            write_float_be(&buf[4], params->ch1_threshold);
            send_frame(own_id, CIMC_FRAME_TYPE_RESPONSE, frame->command, buf, 8);
        }
        return 1;

    case CIMC_CMD_GET_CH0_THRESH:  /* 0x0401 读取CH0阈值 */
        if(frame->length != 0) { send_error(own_id, frame->command); return 1; }
        {
            uint8_t buf[4];
            write_float_be(buf, params->ch0_threshold);
            send_frame(own_id, CIMC_FRAME_TYPE_RESPONSE, frame->command, buf, 4);
        }
        return 1;

    case CIMC_CMD_GET_CH1_THRESH:  /* 0x0402 读取CH1阈值 */
        if(frame->length != 0) { send_error(own_id, frame->command); return 1; }
        {
            uint8_t buf[4];
            write_float_be(buf, params->ch1_threshold);
            send_frame(own_id, CIMC_FRAME_TYPE_RESPONSE, frame->command, buf, 4);
        }
        return 1;

    case CIMC_CMD_SET_CH0_THRESH:  /* 0x0411 写入CH0阈值 */
        if((frame->length != 4) || (NULL == frame->payload)) {
            send_error(own_id, frame->command); return 1;
        }
        cimc_params_set_ch0_threshold(read_float_be(frame->payload));
        send_ok(own_id, frame->command);
        return 1;

    case CIMC_CMD_SET_CH1_THRESH:  /* 0x0412 写入CH1阈值 */
        if((frame->length != 4) || (NULL == frame->payload)) {
            send_error(own_id, frame->command); return 1;
        }
        cimc_params_set_ch1_threshold(read_float_be(frame->payload));
        send_ok(own_id, frame->command);
        return 1;

    case CIMC_CMD_SET_ALARM_MODE:  /* 0x0601 设置告警上报模式（01=主动/02=仅记录） */
        if((frame->length != 1) || (NULL == frame->payload)) {
            send_error(own_id, frame->command); return 1;
        }
        if(0 == cimc_params_set_alarm_mode(frame->payload[0])) {
            send_error(own_id, frame->command); return 1;
        }
        cimc_alarm_set_mode(frame->payload[0]);
        send_ok(own_id, frame->command);
        return 1;

    case CIMC_CMD_GET_ALARM_LOG:  /* 0x0602 查询告警记录（ASCII直接回复，非帧封装） */
        if(frame->length != 0) { send_error(own_id, frame->command); return 1; }
        {
            char buf[560]; /* 10条×55字符+终止符 */
            cimc_alarm_query(buf, (uint16_t)sizeof(buf));
            bsp_usart_send_buffer(RS485_USART,
                                        (const uint8_t *)buf,
                                        (uint16_t)strlen(buf));
        }
        return 1;

    case CIMC_CMD_CLEAR_ALARM:  /* 0x0603 清除告警记录 */
        if(frame->length != 0) { send_error(own_id, frame->command); return 1; }
        cimc_alarm_clear();
        send_ok(own_id, frame->command);
        return 1;

    default:
        /*
         * 命令字不在实现范围内，回错误帧。
         * 未知命令字统一用0xEEEE，与CRC/长度错误保持一致。
         */
        send_error(own_id, 0xEEEEU);
        return 1;
    }
}

/*
 * 处理RS485收到的一帧ASCII十六进制协议数据。
 * 1. ASCII→二进制解码；失败时静默丢弃。
 * 2. 校验帧头/帧尾/长度/版本/CRC-16-Modbus；失败时回错误帧。
 * 3. 通过校验后交由分发器处理。
 */
uint8_t cimc_protocol_process_ascii_frame(const uint8_t *frame, uint16_t length)
{
    uint8_t      binary[BIN_BUF_SIZE];
    uint16_t     binary_length;
    cimc_frame_t parsed_frame;

    binary_length = decode_ascii_hex(frame, length,
                                     binary, (uint16_t)sizeof(binary));
    if(0 == binary_length) {
        return 0;
    }

    if(0 == parse_frame(binary, binary_length, &parsed_frame)) {
        send_error(cimc_params_get()->device_id, 0xEEEEU);
        return 1;
    }

    return dispatch_command(&parsed_frame);
}

/* 发送开机心跳帧（类型0x05，命令字0x8888），通知上位机本机在线 */
void cimc_protocol_send_heartbeat(void)
{
    uint16_t own_id = cimc_params_get()->device_id;
    send_frame(own_id, CIMC_FRAME_TYPE_HEARTBEAT,
                     CIMC_CMD_HEARTBEAT, NULL, 0);
}

/* 调度器100ms周期调用，自动上报激活时按间隔推送数据帧 */
void cimc_protocol_auto_report_tick(void)
{
    const cimc_params_t *params;
    uint32_t             interval_ms;
    uint32_t             now_ms;

    if(0 == cimc_status_is_auto_sample_active()) {
        return;
    }

    params = cimc_params_get();

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

    send_auto_report(params->device_id);
}
