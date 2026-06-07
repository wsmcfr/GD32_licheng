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
#define SOF 0xA5B6U
#define FRAME_END 0xB6A5U
#define PROTO_VER 0x02U
#define FT_CMD 0x01U
#define FT_RSP 0x02U
#define FT_HB 0x05U
#define FT_ERR 0xFFU
#define RSP_OK 0xFFU

/* ── 特殊命令字 ── */
#define CMD_HB 0x8888U
#define CMD_SEARCH 0xFFFFU

/* ── 系统管理类 0x01xx ── */
#define CMD_REBOOT 0x0101U
#define CMD_VERSION 0x0104U
#define CMD_SET_TIME 0x0105U
#define CMD_GET_TIME 0x0106U
#define CMD_SET_ID 0x01A1U
#define CMD_SET_BAUD 0x01A2U
#define CMD_GET_ID 0x0111U
#define CMD_GET_BAUD 0x0112U

/* ── 数据类 0x02xx ── */
#define CMD_GET_CH0 0x0201U
#define CMD_GET_CH1 0x0202U
#define CMD_GET_CH2 0x0221U
#define CMD_SET_R0 0x0241U
#define CMD_SET_R1 0x0242U
#define CMD_SET_INTV 0x0261U

/* ── 控制类 0x03xx ── */
#define CMD_SET_DAC 0x0301U
#define CMD_SAMPLE_ON 0x0302U
#define CMD_SAMPLE_OFF 0x0303U
#define CMD_SLEEP 0x03AAU

/* ── 参数配置类 0x04xx ── */
#define CMD_GET_THRA 0x0400U
#define CMD_GET_THR0 0x0401U
#define CMD_GET_THR1 0x0402U
#define CMD_SET_THR0 0x0411U
#define CMD_SET_THR1 0x0412U

/* ── 升级类 0x05xx ── */
#define CMD_UPGRADE 0x0501U

/* ── 告警日志类 0x06xx ── */
#define CMD_SET_ALM 0x0601U
#define CMD_GET_ALM 0x0602U
#define CMD_CLR_ALM 0x0603U

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
} frame_t;

static uint32_t g_last_report_ms = 0; /* 上次自动上报时刻，用于间隔计时 */

/* ASCII十六进制字符转4位值，非法字符返回0xFF */
static uint8_t hex_nibble(uint8_t ch)
{
    if((ch >= '0') && (ch <= '9')) { return (uint8_t)(ch - '0'); }
    if((ch >= 'A') && (ch <= 'F')) { return (uint8_t)(ch - 'A' + 10); }
    if((ch >= 'a') && (ch <= 'f')) { return (uint8_t)(ch - 'a' + 10); }
    return 0xFFU;
}

// 判断是否为协议允许忽略的空白字符（空格/制表/回车/换行）
static uint8_t is_space(uint8_t ch)
{
    return ((ch == (uint8_t)' ')  || (ch == (uint8_t)'\t') ||
            (ch == (uint8_t)'\r') || (ch == (uint8_t)'\n')) ? 1 : 0;
}

/* 将ASCII十六进制文本解码为二进制字节流，返回字节数，失败返回0 */
static uint16_t decode_ascii_hex(const uint8_t *ascii, uint16_t len,
                                 uint8_t *output, uint16_t sz)
{
    uint16_t ai, out = 0;
    uint8_t hi = 0, hi_ok = 0;

    if((!ascii) || (!output) || (0 == sz)) { return 0; }

    for(ai = 0; ai < len; ai++) {
        uint8_t nibble;
        if(0 != is_space(ascii[ai])) { continue; }
        nibble = hex_nibble(ascii[ai]);
        if(0xFFU == nibble) { return 0; }
        if(0 == hi_ok) {
            hi = nibble; hi_ok = 1;
        } else {
            if(out >= sz) { return 0; }
            output[out++] = (uint8_t)((hi << 4) | nibble);
            hi_ok = 0;
        }
    }
    return (0 != hi_ok) ? 0 : out;
}

// 从大端缓冲区读取16位无符号整数
static uint16_t read_u16_be(const uint8_t *data)
{
    if(!data) { return 0; }
    return (uint16_t)(((uint16_t)data[0] << 8) | (uint16_t)data[1]);
}

// 从大端缓冲区读取32位无符号整数
static uint32_t read_u32_be(const uint8_t *data)
{
    if(!data) { return 0; }
    return ((uint32_t)data[0] << 24) | ((uint32_t)data[1] << 16) |
           ((uint32_t)data[2] <<  8) |  (uint32_t)data[3];
}

/* CRC-16-Modbus校验，覆盖帧头到内容末尾 */
static uint16_t crc16_modbus(const uint8_t *data, uint16_t length)
{
    uint16_t crc = 0xFFFFU;
    uint16_t i;
    uint8_t  b;

    if((!data) && (length > 0)) { return 0; }

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

    if((!output) || (!offset) || ((*offset + 2) >= output_size)) { return 0; }

    output[*offset] = hex_chars[(value >> 4) & 0x0FU]; (*offset)++;
    output[*offset] = hex_chars[ value        & 0x0FU]; (*offset)++;
    output[*offset] = '\0';

    return 1;
}

/*
 * 组装协议帧（二进制→ASCII十六进制）并通过RS485发送。
 * payload为NULL时plen须为0；帧格式：帧头+设备ID+帧类型+命令+长度+版本+内容+CRC+帧尾。
 */
static uint8_t send_frame(uint16_t device_id, uint8_t frame_type,
                          uint16_t command,
                          const uint8_t *payload, uint8_t plen)
{
    uint8_t  binary[BIN_BUF_SIZE];
    char     ascii[ASCII_BUF_SIZE];
    uint16_t n, crc, idx, off = 0;

    if((plen > 0) && (!payload)) { return 0; }

    n = (uint16_t)(13 + plen);
    if(n > sizeof(binary)) { return 0; }

    binary[0] = 0xA5U; binary[1] = 0xB6U;
    binary[2] = (uint8_t)(device_id >> 8);
    binary[3] = (uint8_t)(device_id  & 0xFFU);
    binary[4] = frame_type;
    binary[5] = (uint8_t)(command >> 8);
    binary[6] = (uint8_t)(command  & 0xFFU);
    binary[7] = plen;
    binary[8] = PROTO_VER;
    if(plen > 0) {
        memcpy(&binary[9], payload, plen);
    }

    crc = crc16_modbus(binary, (uint16_t)(9 + plen));
    binary[9  + plen] = (uint8_t)(crc >> 8);
    binary[10 + plen] = (uint8_t)(crc  & 0xFFU);
    binary[11 + plen] = 0xB6U;
    binary[12 + plen] = 0xA5U;

    for(idx = 0; idx < n; idx++) {
        if(0 == append_hex_byte(ascii, (uint16_t)sizeof(ascii),
                                  &off, binary[idx])) {
            return 0;
        }
    }

    bsp_usart_send_buffer(RS485_USART, (const uint8_t *)ascii, off);
    return 1;
}

/* 发送OK应答帧（内容区=0xFF） */
static uint8_t send_ok(uint16_t device_id, uint16_t command)
{
    uint8_t ok = RSP_OK;
    return send_frame(device_id, FT_RSP, command, &ok, 1);
}

/* 发送错误应答帧（帧类型0xFF），CRC/长度/非法帧类型时回复 */
static uint8_t send_error(uint16_t device_id, uint16_t command)
{
    return send_frame(device_id, FT_ERR, command, NULL, 0);
}

/* 采集UTC+CH0+CH1组装12字节payload，发送一帧自动上报数据帧 */
static void send_auto_report(uint16_t own_id)
{
    const params_t *params = params_get();
    uint8_t  payload[12];
    uint32_t unix_ts = 0;
    float    ch0, ch1;

    rtc_app_get_unix_epoch(&unix_ts);
    ch0 = adc_app_get_ch0_raw_float() * params->ch0_ratio;
    ch1 = adc_app_get_ch1_raw_float() * params->ch1_ratio;

    write_u32_be(&payload[0], unix_ts);
    write_float_be(&payload[4], ch0);
    write_float_be(&payload[8], ch1);

    send_frame(own_id, FT_RSP,
                     CMD_SAMPLE_ON, payload, 12);
}

/* 校验并解析一帧二进制协议数据，合法返回1，否则返回0 */
static uint8_t parse_frame(const uint8_t *binary, uint16_t binary_len,
                            frame_t *frame)
{
    uint8_t  plen;
    uint16_t elen;
    uint16_t rcrc, ccrc;

    if((!binary) || (!frame) || (binary_len < 13)) { return 0; }

    if((SOF != read_u16_be(&binary[0])) ||
       (FRAME_END   != read_u16_be(&binary[binary_len - 2]))) {
        return 0;
    }

    plen  = binary[7];
    elen = (uint16_t)(13 + plen);
    if(binary_len != elen) { return 0; }

    if(PROTO_VER != binary[8]) { return 0; }

    rcrc = read_u16_be(&binary[9 + plen]);
    ccrc = crc16_modbus(binary, (uint16_t)(9 + plen));
    if(rcrc != ccrc) { return 0; }

    frame->device_id  = read_u16_be(&binary[2]);
    frame->frame_type = binary[4];
    frame->command    = read_u16_be(&binary[5]);
    frame->length     = plen;
    frame->version    = binary[8];
    frame->payload    = (plen > 0) ? &binary[9] : NULL;

    return 1;
}

/*
 * 执行已校验帧对应的命令。
 * 设备ID过滤：0xFFFF=广播，等于本机ID=正常响应，其他静默丢弃。
 * 自动上报激活期间只允许停止命令（0x0303），其他命令静默丢弃。
 */
static uint8_t dispatch_command(const frame_t *frame)
{
    const params_t *params;
    uint16_t own_id;

    if(!frame) { return 0; }

    params = params_get();
    own_id = params->device_id;

    if((frame->device_id != own_id) && (frame->device_id != 0xFFFFU)) {
        return 1;
    }

    if(0 != sts_sampling()) {
        if((FT_CMD != frame->frame_type) ||
           (frame->command != CMD_SAMPLE_OFF)) {
            return 1;
        }
    }

    /* 心跳帧（类型0x05）：广播寻址时回复本机心跳 */
    if(FT_HB == frame->frame_type) {
        if(CMD_SEARCH == frame->command) {
            send_frame(own_id, FT_HB,
                             CMD_HB, NULL, 0);
        }
        return 1;
    }

    /* 非命令帧（类型非0x01）→ 回错误帧 */
    if(FT_CMD != frame->frame_type) {
        send_error(own_id, frame->command);
        return 1;
    }

    switch(frame->command) {

    case CMD_REBOOT:  /* 0x0101 设备重启 */
        if(frame->length != 0) { send_error(own_id, frame->command); return 1; }
        send_ok(own_id, frame->command);
        alm_save();
        delay_ms(20);
        __set_FAULTMASK(1);
        NVIC_SystemReset();
        return 1;

    case CMD_VERSION:  /* 0x0104 查询固件版本 */
        if(frame->length != 0) { send_error(own_id, frame->command); return 1; }
        send_frame(own_id, FT_RSP,
                         frame->command, s_fw_ver, 4);
        return 1;

    case CMD_SET_TIME:  /* 0x0105 设置设备时间（4字节UTC秒，大端） */
        if((frame->length != 4) || (!frame->payload)) {
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

    case CMD_GET_TIME:  /* 0x0106 查询设备时间（回4字节UTC秒） */
        if(frame->length != 0) { send_error(own_id, frame->command); return 1; }
        {
            uint32_t ts;
            uint8_t  buf[4];
            if(0 != rtc_app_get_unix_epoch(&ts)) {
                send_error(own_id, frame->command); return 1;
            }
            write_u32_be(buf, ts);
            send_frame(own_id, FT_RSP, frame->command, buf, 4);
        }
        return 1;

    case CMD_GET_ID:  /* 0x0111 查询设备ID（广播下发，回本机2字节ID） */
        if(frame->length != 0) { send_error(own_id, frame->command); return 1; }
        {
            uint8_t buf[2];
            buf[0] = (uint8_t)(own_id >> 8);
            buf[1] = (uint8_t)(own_id  & 0xFFU);
            send_frame(own_id, FT_RSP, frame->command, buf, 2);
        }
        return 1;

    case CMD_GET_BAUD:  /* 0x0112 查询波特率（回1字节映射码） */
        if(frame->length != 0) { send_error(own_id, frame->command); return 1; }
        {
            uint8_t baud_code = params->baud_code;
            send_frame(own_id, FT_RSP, frame->command, &baud_code, 1);
        }
        return 1;

    case CMD_SET_ID:  /* 0x01A1 设置设备ID（应答帧用新ID） */
        if((frame->length != 2) || (!frame->payload)) {
            send_error(own_id, frame->command); return 1;
        }
        {
            uint16_t new_id = read_u16_be(frame->payload);
            if(0 == params_set_id(new_id)) {
                send_error(own_id, frame->command); return 1;
            }
            send_ok(new_id, frame->command);
        }
        return 1;

    case CMD_SET_BAUD:  /* 0x01A2 设置波特率：先回OK（旧波特率），再在线切换 */
        if((frame->length != 1) || (!frame->payload)) {
            send_error(own_id, frame->command); return 1;
        }
        if(0 == params_set_baud(frame->payload[0])) {
            send_error(own_id, frame->command); return 1;
        }
        send_ok(own_id, frame->command);
        /*
         * OK帧以旧波特率发完后，在线切换到新波特率，不重启。
         * 波特率码已由params_set_baud()持久化到Flash。
         */
        delay_ms(20);
        bsp_usart_change_baudrate(params_baud());
        return 1;

    case CMD_SET_DAC:  /* 0x0301 设置DAC输出（0~4095） */
        if((frame->length != 2) || (!frame->payload)) {
            send_error(own_id, frame->command); return 1;
        }
        {
            uint16_t dac_val = read_u16_be(frame->payload);
            if(dac_val > 0x0FFFU) { send_error(own_id, frame->command); return 1; }
            adc_app_set_dac_raw(dac_val);
            send_ok(own_id, frame->command);
        }
        return 1;

    case CMD_SAMPLE_ON:  /* 0x0302 开始定时自动上报，首次响应直接发数据帧 */
        if(frame->length != 0) { send_error(own_id, frame->command); return 1; }
        sts_set_sample(1);
        send_auto_report(own_id);
        g_last_report_ms = timebase_get_ms32();
        return 1;

    case CMD_SAMPLE_OFF:  /* 0x0303 停止定时自动上报 */
        if(frame->length != 0) { send_error(own_id, frame->command); return 1; }
        sts_set_sample(0);
        send_ok(own_id, frame->command);
        return 1;

    case CMD_UPGRADE:  /* 0x0501 进入Bootloader等待升级 */
        if(frame->length != 0) { send_error(own_id, frame->command); return 1; }
        if(BOOTLOADER_PORT_STATUS_OK != bootloader_port_request_bootloader_upgrade()) {
            send_error(own_id, frame->command); return 1;
        }
        send_ok(own_id, frame->command);
        alm_save();
        delay_ms(20);
        bootloader_port_request_upgrade_reset();
        return 1;

    case CMD_GET_CH0:  /* 0x0201 查询CH0（原始电压×变比，float大端） */
        if(frame->length != 0) { send_error(own_id, frame->command); return 1; }
        {
            uint8_t buf[4];
            float   val = adc_app_get_ch0_raw_float() * params->ch0_ratio;
            write_float_be(buf, val);
            send_frame(own_id, FT_RSP, frame->command, buf, 4);
        }
        return 1;

    case CMD_GET_CH1:  /* 0x0202 查询CH1（原始电压×变比，float大端） */
        if(frame->length != 0) { send_error(own_id, frame->command); return 1; }
        {
            uint8_t buf[4];
            float   val = adc_app_get_ch1_raw_float() * params->ch1_ratio;
            write_float_be(buf, val);
            send_frame(own_id, FT_RSP, frame->command, buf, 4);
        }
        return 1;

    case CMD_GET_CH2:  /* 0x0221 查询CH2（PT100温度，float大端） */
        if(frame->length != 0) { send_error(own_id, frame->command); return 1; }
        {
            uint8_t          buf[4];
            pt100_measurement_t meas = gd30ad3344_pt100_get_latest();
            write_float_be(buf, meas.temperature_c);
            send_frame(own_id, FT_RSP, frame->command, buf, 4);
        }
        return 1;

    case CMD_SET_R0:  /* 0x0241 设置CH0变比 */
        if((frame->length != 4) || (!frame->payload)) {
            send_error(own_id, frame->command); return 1;
        }
        params_set_r0(read_float_be(frame->payload));
        send_ok(own_id, frame->command);
        return 1;

    case CMD_SET_R1:  /* 0x0242 设置CH1变比 */
        if((frame->length != 4) || (!frame->payload)) {
            send_error(own_id, frame->command); return 1;
        }
        params_set_r1(read_float_be(frame->payload));
        send_ok(own_id, frame->command);
        return 1;

    case CMD_SET_INTV:  /* 0x0261 设置自动上报间隔（01=1s/02=3s/03=5s） */
        if((frame->length != 1) || (!frame->payload)) {
            send_error(own_id, frame->command); return 1;
        }
        if(0 == params_set_intv(frame->payload[0])) {
            send_error(own_id, frame->command); return 1;
        }
        send_ok(own_id, frame->command);
        return 1;

    case CMD_SLEEP:  /* 0x03AA 进入MCU深度睡眠10s后唤醒 */
        if(frame->length != 0) { send_error(own_id, frame->command); return 1; }
        send_ok(own_id, frame->command);
        power_sleep();
        return 1;

    case CMD_GET_THRA:  /* 0x0400 批量读取CH0+CH1阈值（各float大端，共8字节） */
        if(frame->length != 0) { send_error(own_id, frame->command); return 1; }
        {
            uint8_t buf[8];
            write_float_be(&buf[0], params->ch0_threshold);
            write_float_be(&buf[4], params->ch1_threshold);
            send_frame(own_id, FT_RSP, frame->command, buf, 8);
        }
        return 1;

    case CMD_GET_THR0:  /* 0x0401 读取CH0阈值 */
        if(frame->length != 0) { send_error(own_id, frame->command); return 1; }
        {
            uint8_t buf[4];
            write_float_be(buf, params->ch0_threshold);
            send_frame(own_id, FT_RSP, frame->command, buf, 4);
        }
        return 1;

    case CMD_GET_THR1:  /* 0x0402 读取CH1阈值 */
        if(frame->length != 0) { send_error(own_id, frame->command); return 1; }
        {
            uint8_t buf[4];
            write_float_be(buf, params->ch1_threshold);
            send_frame(own_id, FT_RSP, frame->command, buf, 4);
        }
        return 1;

    case CMD_SET_THR0:  /* 0x0411 写入CH0阈值 */
        if((frame->length != 4) || (!frame->payload)) {
            send_error(own_id, frame->command); return 1;
        }
        params_set_thr0(read_float_be(frame->payload));
        send_ok(own_id, frame->command);
        return 1;

    case CMD_SET_THR1:  /* 0x0412 写入CH1阈值 */
        if((frame->length != 4) || (!frame->payload)) {
            send_error(own_id, frame->command); return 1;
        }
        params_set_thr1(read_float_be(frame->payload));
        send_ok(own_id, frame->command);
        return 1;

    case CMD_SET_ALM:  /* 0x0601 设置告警上报模式（01=主动/02=仅记录） */
        if((frame->length != 1) || (!frame->payload)) {
            send_error(own_id, frame->command); return 1;
        }
        if(0 == params_set_alm(frame->payload[0])) {
            send_error(own_id, frame->command); return 1;
        }
        alm_set_mode(frame->payload[0]);
        send_ok(own_id, frame->command);
        return 1;

    case CMD_GET_ALM:  /* 0x0602 查询告警记录（ASCII直接回复，非帧封装） */
        if(frame->length != 0) { send_error(own_id, frame->command); return 1; }
        {
            char buf[560]; /* 10条×55字符+终止符 */
            alm_query(buf, (uint16_t)sizeof(buf));
            bsp_usart_send_buffer(RS485_USART,
                                        (const uint8_t *)buf,
                                        (uint16_t)strlen(buf));
        }
        return 1;

    case CMD_CLR_ALM:  /* 0x0603 清除告警记录 */
        if(frame->length != 0) { send_error(own_id, frame->command); return 1; }
        alm_clear();
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
uint8_t proto_rx(const uint8_t *frame, uint16_t length)
{
    uint8_t      binary[BIN_BUF_SIZE];
    uint16_t     binary_length;
    frame_t parsed_frame;

    binary_length = decode_ascii_hex(frame, length,
                                     binary, (uint16_t)sizeof(binary));
    if(0 == binary_length) {
        return 0;
    }

    if(0 == parse_frame(binary, binary_length, &parsed_frame)) {
        send_error(params_get()->device_id, 0xEEEEU);
        return 1;
    }

    return dispatch_command(&parsed_frame);
}

/* 发送开机心跳帧（类型0x05，命令字0x8888），通知上位机本机在线 */
void proto_hb(void)
{
    uint16_t own_id = params_get()->device_id;
    send_frame(own_id, FT_HB,
                     CMD_HB, NULL, 0);
}

/* 调度器100ms周期调用，自动上报激活时按间隔推送数据帧 */
void proto_tick(void)
{
    const params_t *params;
    uint32_t             interval_ms;
    uint32_t             now_ms;

    if(0 == sts_sampling()) {
        return;
    }

    params = params_get();

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
