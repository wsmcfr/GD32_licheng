#include "cimc_alarm.h"
#include "bsp_usart.h"
#include "bsp_rtc.h"
#include "bootloader_port.h"

/* 最多保存 10 条告警记录，每条最长 56 字节（含 '\n' 和 '\0'）。 */
#define CIMC_ALARM_MAX_RECORDS  10U
#define CIMC_ALARM_RECORD_LEN   56U

/* 同一通道两次告警之间的最小间隔，防止 50ms ADC 任务高频刷入。 */
#define CIMC_ALARM_DEBOUNCE_MS  1000UL

/*
 * Flash 持久化相关定义。
 * 告警块紧跟在 user_config 区的 cimc_params_t（29字节，packed）之后。
 * 若修改 cimc_params_t 结构体大小，必须同步更新此常量。
 */
#define CIMC_ALARM_FLASH_MAGIC   0xA1B2C3D4UL
#define CIMC_ALARM_FLASH_OFFSET  29U   /* sizeof(cimc_params_t) packed */

/*
 * 结构体作用：
 *   一条告警记录的 Flash 二进制表示。
 *   存储时间字段+通道+阈值+实际值，共 16 字节。
 *   加载时从二进制重建 ASCII 字符串写入 g_records[]。
 */
typedef struct __attribute__((packed))
{
    uint16_t year;
    uint8_t  month;
    uint8_t  date;
    uint8_t  hour;
    uint8_t  minute;
    uint8_t  second;
    uint8_t  channel;
    float    threshold;
    float    value;
} cimc_alarm_flash_record_t;

/*
 * 结构体作用：
 *   Flash 中告警块的完整布局（169 字节）。
 *   user_config[29..197] = magic(4) + count(1) + records[10](160) + crc32(4)。
 */
typedef struct __attribute__((packed))
{
    uint32_t magic;
    uint8_t  count;
    cimc_alarm_flash_record_t records[CIMC_ALARM_MAX_RECORDS];
    uint32_t crc32;
} cimc_alarm_flash_t;

/* CRC32 覆盖范围：magic + count + records，不含末尾 crc32 字段本身。 */
#define CIMC_ALARM_CRC_DATA_SIZE  ((uint16_t)(sizeof(cimc_alarm_flash_t) - sizeof(uint32_t)))

/* 读写 user_config 时的 RAM 缓冲区总大小：params(29) + alarm_block(169) = 198 字节。 */
#define CIMC_ALARM_FLASH_BUF_SIZE ((uint16_t)(CIMC_ALARM_FLASH_OFFSET + sizeof(cimc_alarm_flash_t)))

/*
 * 告警记录环形缓冲：新记录追加到末尾，超出 MAX 时淘汰最旧（索引 0）。
 * 查询时按时间倒序输出（从 g_count-1 到 0）。
 */
static char    g_records[CIMC_ALARM_MAX_RECORDS][CIMC_ALARM_RECORD_LEN];
static uint8_t g_count;   /* 当前有效记录数，最大 CIMC_ALARM_MAX_RECORDS */
static uint8_t g_mode;    /* 0x01 主动上报 / 0x02 仅记录 */

/* 每通道上次触发告警的毫秒时刻，用于 1s 去抖。 */
static uint32_t g_last_alarm_ms[2];

/* 与 g_records[] 同步的 Flash 二进制记录数组，用于持久化写入 Flash。 */
static cimc_alarm_flash_record_t g_flash_records[CIMC_ALARM_MAX_RECORDS];

/*
 * 函数作用：
 *   初始化告警模块：清空记录表，将模式重置为仅记录（0x02）。
 */
void cimc_alarm_init(void)
{
    uint8_t i;

    g_count = 0U;
    g_mode  = 0x02U;

    for(i = 0U; i < 2U; i++) {
        g_last_alarm_ms[i] = 0UL;
    }
}

/*
 * 函数作用：
 *   设置告警上报模式（0x01 主动 / 0x02 仅记录）；非法值忽略。
 */
void cimc_alarm_set_mode(uint8_t mode)
{
    if((mode == 0x01U) || (mode == 0x02U)) {
        g_mode = mode;
    }
}

/*
 * 函数作用：
 *   获取当前告警上报模式。
 */
uint8_t cimc_alarm_get_mode(void)
{
    return g_mode;
}

/*
 * 函数作用：
 *   将告警数据写入 user_config 区（参数区之后的剩余空间）持久化到 Flash。
 * 主要流程：
 *   1. 读出 user_config 前 198 字节（params 29B + alarm 169B）到 RAM 缓冲。
 *   2. 在 RAM 缓冲偏移 29 处填写 cimc_alarm_flash_t 块。
 *   3. 计算 CRC32 并回写 198 字节。
 */
static void prv_cimc_alarm_save_flash(void)
{
    uint8_t buf[CIMC_ALARM_FLASH_BUF_SIZE];
    cimc_alarm_flash_t *flash_block;
    uint8_t i;

    /* 先读出当前 user_config 前部（保留 params 字段不改动）。 */
    if(BOOTLOADER_PORT_STATUS_OK != bootloader_port_read_user_config(
            buf, CIMC_ALARM_FLASH_BUF_SIZE)) {
        return;
    }

    /* 定位到告警块起始位置并填写字段。 */
    flash_block = (cimc_alarm_flash_t *)&buf[CIMC_ALARM_FLASH_OFFSET];
    flash_block->magic = CIMC_ALARM_FLASH_MAGIC;
    flash_block->count = g_count;

    for(i = 0U; i < g_count; i++) {
        flash_block->records[i] = g_flash_records[i];
    }

    /* CRC32 覆盖 magic + count + records，不含 crc32 字段本身。 */
    flash_block->crc32 = bootloader_port_crc32_calc(
        (const uint8_t *)flash_block, CIMC_ALARM_CRC_DATA_SIZE);

    (void)bootloader_port_write_user_config(buf, CIMC_ALARM_FLASH_BUF_SIZE);
}

/*
 * 函数作用：
 *   将一条告警字符串和对应的二进制记录追加到缓冲区。
 *   若已满（10 条），丢弃最旧的一条（整体前移），再把新记录追加到末尾。
 * 参数说明：
 *   text：格式化好的单行告警字符串，末尾含 '\n'。
 *   flash_rec：对应的 Flash 二进制记录，与 text 内容一致。
 */
static void prv_alarm_append(const char *text, const cimc_alarm_flash_record_t *flash_rec)
{
    uint8_t i;

    if(g_count >= CIMC_ALARM_MAX_RECORDS) {
        /* 缓冲区满：整体前移，腾出末尾位置（同步移动两个数组）。 */
        for(i = 0U; i < (CIMC_ALARM_MAX_RECORDS - 1U); i++) {
            memcpy(g_records[i], g_records[i + 1U], CIMC_ALARM_RECORD_LEN);
            g_flash_records[i] = g_flash_records[i + 1U];
        }
        g_count = (uint8_t)(CIMC_ALARM_MAX_RECORDS - 1U);
    }

    /* 写入到末尾并截断，保证不越界。 */
    (void)snprintf(g_records[g_count], CIMC_ALARM_RECORD_LEN, "%s", text);
    g_flash_records[g_count] = *flash_rec;
    g_count++;
}

/*
 * 函数作用：
 *   每次 ADC 采集后调用，判断是否超阈并按模式处理。
 * 主要流程：
 *   1. 检查 value > threshold；未超阈直接返回。
 *   2. 检查 1s 去抖；距上次同通道告警不足 1s 则返回。
 *   3. 获取 RTC 当前时间，格式化告警字符串。
 *   4. 若主动上报模式（0x01），通过 RS485 直接发送字符串。
 *   5. 追加到 RAM 缓冲并写入 Flash 持久化。
 */
void cimc_alarm_check(uint8_t channel, float threshold, float value)
{
    uint32_t now_ms;
    bsp_rtc_datetime_t dt;
    char text[CIMC_ALARM_RECORD_LEN];
    cimc_alarm_flash_record_t flash_rec;

    if(channel > 1U) { return; }

    if(value <= threshold) { return; }

    /* 1s 去抖：同一通道连续超阈时每秒最多一次告警。 */
    now_ms = timebase_get_ms32();
    if((uint32_t)(now_ms - g_last_alarm_ms[channel]) < CIMC_ALARM_DEBOUNCE_MS) {
        return;
    }
    g_last_alarm_ms[channel] = now_ms;

    /* 读取当前 RTC 时间用于时间戳。 */
    if(0 != bsp_rtc_get_datetime(&dt)) {
        /*
         * RTC 读取失败时仍然记录告警，只是时间戳用全零。
         * 不因 RTC 异常而丢弃告警。
         */
        memset(&dt, 0, sizeof(dt));
    }

    (void)snprintf(text, sizeof(text),
                   "%04u-%02u-%02u %02u:%02u:%02u | CH%u | %.2f | %.2f\n",
                   (unsigned)dt.year, (unsigned)dt.month, (unsigned)dt.date,
                   (unsigned)dt.hour, (unsigned)dt.minute, (unsigned)dt.second,
                   (unsigned)channel, (double)threshold, (double)value);

    /* 构建 Flash 二进制记录，与文本内容完全对应。 */
    flash_rec.year      = dt.year;
    flash_rec.month     = dt.month;
    flash_rec.date      = dt.date;
    flash_rec.hour      = dt.hour;
    flash_rec.minute    = dt.minute;
    flash_rec.second    = dt.second;
    flash_rec.channel   = channel;
    flash_rec.threshold = threshold;
    flash_rec.value     = value;

    /*
     * 主动上报模式：直接向 RS485 发送 ASCII 字符串，不经帧封装。
     * 赛题特别说明："时间|通道|阈值|实际值（非帧封装，ASCII字符串直接回复）"
     */
    if(0x01U == g_mode) {
        (void)bsp_usart_send_buffer(RS485_USART,
                                    (const uint8_t *)text,
                                    (uint16_t)strlen(text));
    }

    prv_alarm_append(text, &flash_rec);

    /* 每次新增告警后立即持久化，确保重启后能恢复。 */
    prv_cimc_alarm_save_flash();
}

/*
 * 函数作用：
 *   将所有告警记录以时间倒序写入 buf；无记录时写入 "empty"。
 * 主要流程：
 *   从 g_count-1 到 0 逐条追加，超出 size 时截止。
 */
void cimc_alarm_query(char *buf, uint16_t size)
{
    uint16_t pos = 0U;
    uint16_t rec_len;
    int8_t   i;

    if((NULL == buf) || (0U == size)) { return; }

    if(0U == g_count) {
        (void)snprintf(buf, size, "empty");
        return;
    }

    for(i = (int8_t)(g_count - 1U); i >= 0; i--) {
        rec_len = (uint16_t)strlen(g_records[i]);
        if((pos + rec_len) >= size) { break; }
        memcpy(&buf[pos], g_records[i], rec_len);
        pos += rec_len;
    }
    buf[pos] = '\0';
}

/*
 * 函数作用：
 *   清除所有告警记录，将计数归零，并立即写 Flash 持久化清除状态。
 */
void cimc_alarm_clear(void)
{
    g_count = 0U;
    prv_cimc_alarm_save_flash();
}

/*
 * 函数作用：
 *   从 Flash user_config 区恢复历史告警记录到 RAM。
 *   应在 cimc_alarm_init() 之后调用，确保 RAM 清零后再叠加 Flash 数据。
 * 主要流程：
 *   1. 读取 user_config 前 198 字节，定位到告警块（偏移 29）。
 *   2. 校验魔术字和 CRC32；任一失败则视为无历史记录，静默返回。
 *   3. 将二进制记录逐条转换回 ASCII 字符串，重建 g_records[] 和 g_flash_records[]。
 */
void cimc_alarm_load(void)
{
    uint8_t buf[CIMC_ALARM_FLASH_BUF_SIZE];
    const cimc_alarm_flash_t *flash_block;
    uint32_t calc_crc;
    uint8_t i;
    bsp_rtc_datetime_t dt;
    char text[CIMC_ALARM_RECORD_LEN];

    if(BOOTLOADER_PORT_STATUS_OK != bootloader_port_read_user_config(
            buf, CIMC_ALARM_FLASH_BUF_SIZE)) {
        return;
    }

    flash_block = (const cimc_alarm_flash_t *)&buf[CIMC_ALARM_FLASH_OFFSET];

    /* 魔术字不匹配：用户配置区从未写入过告警数据，视为空记录。 */
    if(flash_block->magic != CIMC_ALARM_FLASH_MAGIC) {
        return;
    }

    /* CRC32 校验：保护 magic + count + records 字段完整性。 */
    calc_crc = bootloader_port_crc32_calc(
        (const uint8_t *)flash_block, CIMC_ALARM_CRC_DATA_SIZE);
    if(calc_crc != flash_block->crc32) {
        return;
    }

    /* 记录数合法性检查，防止损坏数据导致越界。 */
    if(flash_block->count > CIMC_ALARM_MAX_RECORDS) {
        return;
    }

    g_count = flash_block->count;

    for(i = 0U; i < g_count; i++) {
        /* 恢复 Flash 二进制记录。 */
        g_flash_records[i] = flash_block->records[i];

        /* 将二进制字段还原为 ASCII 字符串，供 cimc_alarm_query() 使用。 */
        dt.year   = flash_block->records[i].year;
        dt.month  = flash_block->records[i].month;
        dt.date   = flash_block->records[i].date;
        dt.hour   = flash_block->records[i].hour;
        dt.minute = flash_block->records[i].minute;
        dt.second = flash_block->records[i].second;

        (void)snprintf(text, sizeof(text),
                       "%04u-%02u-%02u %02u:%02u:%02u | CH%u | %.2f | %.2f\n",
                       (unsigned)dt.year, (unsigned)dt.month, (unsigned)dt.date,
                       (unsigned)dt.hour, (unsigned)dt.minute, (unsigned)dt.second,
                       (unsigned)flash_block->records[i].channel,
                       (double)flash_block->records[i].threshold,
                       (double)flash_block->records[i].value);

        (void)snprintf(g_records[i], CIMC_ALARM_RECORD_LEN, "%s", text);
    }
}
