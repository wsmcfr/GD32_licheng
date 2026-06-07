#include "cimc_alarm.h"
#include "bsp_usart.h"
#include "bsp_rtc.h"
#include "bootloader_port.h"

#define MAX_RECORDS     10    /* 最多保存10条告警记录 */
#define RECORD_LEN      56    /* 每条最长56字节 */
#define DEBOUNCE_MS     1000UL /* 同一通道两次告警最小间隔 */

/*
 * Flash持久化：告警块紧跟cimc_params_t（29字节packed）之后。
 * 若修改cimc_params_t大小，必须同步更新ALARM_FLASH_OFFSET。
 */
#define ALARM_FLASH_MAGIC   0xA1B2C3D4UL
#define ALARM_FLASH_OFFSET  29

/* 一条告警记录的Flash二进制表示（16字节） */
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
} alarm_flash_rec_t;

/* Flash告警块：magic(4)+count(1)+records(160)+crc32(4) = 169字节 */
typedef struct __attribute__((packed))
{
    uint32_t       magic;
    uint8_t        count;
    alarm_flash_rec_t records[MAX_RECORDS];
    uint32_t       crc32;
} alarm_flash_blk_t;

#define ALARM_CRC_SIZE  ((uint16_t)(sizeof(alarm_flash_blk_t) - sizeof(uint32_t)))
#define ALARM_BUF_SIZE  ((uint16_t)(ALARM_FLASH_OFFSET + sizeof(alarm_flash_blk_t)))

/* 告警文本环形缓冲：新记录追加末尾，满后丢弃最旧 */
static char           g_records[MAX_RECORDS][RECORD_LEN];
static uint8_t        g_count;         /* 当前有效记录数 */
static uint8_t        g_mode;          /* 0x01主动上报 / 0x02仅记录 */
static uint32_t       g_last_ms[2];    /* 每通道上次告警时刻，用于去抖 */
static alarm_flash_rec_t g_flash[MAX_RECORDS]; /* 与g_records同步的二进制记录 */

// 初始化告警模块，清空记录，模式置为仅记录
void cimc_alarm_init(void)
{
    uint8_t i;

    g_count = 0;
    g_mode  = 0x02U;

    for(i = 0; i < 2; i++) {
        g_last_ms[i] = 0;
    }
}

/* 设置告警上报模式（0x01主动/0x02仅记录），非法值忽略 */
void cimc_alarm_set_mode(uint8_t mode)
{
    if((mode == 0x01U) || (mode == 0x02U)) {
        g_mode = mode;
    }
}

// 获取当前告警上报模式
uint8_t cimc_alarm_get_mode(void)
{
    return g_mode;
}

/* 将告警块写入Flash user_config区（偏移ALARM_FLASH_OFFSET处） */
static void save_flash(void)
{
    uint8_t buf[ALARM_BUF_SIZE];
    alarm_flash_blk_t *blk;
    uint8_t i;

    if(BOOTLOADER_PORT_STATUS_OK != bootloader_port_read_user_config(
            buf, ALARM_BUF_SIZE)) {
        return;
    }

    blk = (alarm_flash_blk_t *)&buf[ALARM_FLASH_OFFSET];
    blk->magic = ALARM_FLASH_MAGIC;
    blk->count = g_count;

    for(i = 0; i < g_count; i++) {
        blk->records[i] = g_flash[i];
    }

    blk->crc32 = bootloader_port_crc32_calc(
        (const uint8_t *)blk, ALARM_CRC_SIZE);

    bootloader_port_write_user_config(buf, ALARM_BUF_SIZE);
}

/* 追加一条告警到缓冲区，满10条时淘汰最旧 */
static void append_record(const char *text, const alarm_flash_rec_t *rec)
{
    uint8_t i;

    if(g_count >= MAX_RECORDS) {
        for(i = 0; i < (MAX_RECORDS - 1); i++) {
            memcpy(g_records[i], g_records[i + 1], RECORD_LEN);
            g_flash[i] = g_flash[i + 1];
        }
        g_count = (uint8_t)(MAX_RECORDS - 1);
    }

    snprintf(g_records[g_count], RECORD_LEN, "%s", text);
    g_flash[g_count] = *rec;
    g_count++;
}

/*
 * ADC采集后调用：value超threshold且距上次同通道>=1s时记录告警。
 * 主动模式（0x01）直接向RS485发ASCII字符串，不经帧封装。
 * 此处不写Flash（整页擦写约1.5s会阻塞CPU），持久化在重启前统一完成。
 */
void cimc_alarm_check(uint8_t channel, float threshold, float value)
{
    uint32_t now_ms;
    bsp_rtc_datetime_t dt;
    char text[RECORD_LEN];
    alarm_flash_rec_t rec;

    if(channel > 1) { return; }
    if(value <= threshold) { return; }

    now_ms = timebase_get_ms32();
    if((uint32_t)(now_ms - g_last_ms[channel]) < DEBOUNCE_MS) {
        return;
    }
    g_last_ms[channel] = now_ms;

    if(0 != bsp_rtc_get_datetime(&dt)) {
        memset(&dt, 0, sizeof(dt));
    }

    snprintf(text, sizeof(text),
                   "%04u-%02u-%02u %02u:%02u:%02u | CH%u | %.2f | %.2f\n",
                   (unsigned)dt.year, (unsigned)dt.month, (unsigned)dt.date,
                   (unsigned)dt.hour, (unsigned)dt.minute, (unsigned)dt.second,
                   (unsigned)channel, (double)threshold, (double)value);

    rec.year      = dt.year;
    rec.month     = dt.month;
    rec.date      = dt.date;
    rec.hour      = dt.hour;
    rec.minute    = dt.minute;
    rec.second    = dt.second;
    rec.channel   = channel;
    rec.threshold = threshold;
    rec.value     = value;

    if(0x01U == g_mode) {
        bsp_usart_send_buffer(RS485_USART,
                                    (const uint8_t *)text,
                                    (uint16_t)strlen(text));
    }

    append_record(text, &rec);
}

/* 将所有告警记录倒序写入buf，无记录时写"empty" */
void cimc_alarm_query(char *buf, uint16_t size)
{
    uint16_t pos = 0;
    uint16_t rec_len;
    int8_t   i;

    if((NULL == buf) || (0 == size)) { return; }

    if(0 == g_count) {
        snprintf(buf, size, "empty");
        return;
    }

    for(i = (int8_t)(g_count - 1); i >= 0; i--) {
        rec_len = (uint16_t)strlen(g_records[i]);
        if((pos + rec_len) >= size) { break; }
        memcpy(&buf[pos], g_records[i], rec_len);
        pos += rec_len;
    }
    buf[pos] = '\0';
}

/*
 * 清除所有告警记录并重置去抖计时器。
 * 不在此写Flash：整页擦写期间若复位，重启后load()会恢复旧数据使清除失效。
 * 重置计时器防止adc_task在清除后1s内立即写入新记录。
 */
void cimc_alarm_clear(void)
{
    uint8_t  i;
    uint32_t now = timebase_get_ms32();

    g_count = 0;

    for(i = 0; i < 2; i++)
    {
        g_last_ms[i] = now;
    }
}

/* 将当前RAM告警记录持久化到Flash，应在设备即将重启前调用 */
void cimc_alarm_save(void)
{
    save_flash();
}

/*
 * 从Flash恢复历史告警记录到RAM，应在cimc_alarm_init()之后调用。
 * 魔术字或CRC校验失败时静默返回，视为无历史记录。
 */
void cimc_alarm_load(void)
{
    uint8_t buf[ALARM_BUF_SIZE];
    const alarm_flash_blk_t *blk;
    uint32_t calc_crc;
    uint8_t i;
    bsp_rtc_datetime_t dt;
    char text[RECORD_LEN];

    if(BOOTLOADER_PORT_STATUS_OK != bootloader_port_read_user_config(
            buf, ALARM_BUF_SIZE)) {
        return;
    }

    blk = (const alarm_flash_blk_t *)&buf[ALARM_FLASH_OFFSET];

    if(blk->magic != ALARM_FLASH_MAGIC) { return; }

    calc_crc = bootloader_port_crc32_calc(
        (const uint8_t *)blk, ALARM_CRC_SIZE);
    if(calc_crc != blk->crc32) { return; }

    if(blk->count > MAX_RECORDS) { return; }

    g_count = blk->count;

    for(i = 0; i < g_count; i++) {
        g_flash[i] = blk->records[i];

        dt.year   = blk->records[i].year;
        dt.month  = blk->records[i].month;
        dt.date   = blk->records[i].date;
        dt.hour   = blk->records[i].hour;
        dt.minute = blk->records[i].minute;
        dt.second = blk->records[i].second;

        snprintf(text, sizeof(text),
                       "%04u-%02u-%02u %02u:%02u:%02u | CH%u | %.2f | %.2f\n",
                       (unsigned)dt.year, (unsigned)dt.month, (unsigned)dt.date,
                       (unsigned)dt.hour, (unsigned)dt.minute, (unsigned)dt.second,
                       (unsigned)blk->records[i].channel,
                       (double)blk->records[i].threshold,
                       (double)blk->records[i].value);

        snprintf(g_records[i], RECORD_LEN, "%s", text);
    }
}
