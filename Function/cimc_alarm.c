#include "cimc_alarm.h"
#include "bsp_usart.h"
#include "bsp_rtc.h"
#include "bootloader_port.h"

#define MAX_RECORDS     10    /* 记录数 */
#define RECORD_LEN      56    /* 单条长度 */
#define DEBOUNCE_MS     1000  /* 去抖时间 */

/* 告警块紧跟params_t，修改params_t时同步ALM_OFFSET。 */
#define ALM_MAGIC   0xA1B2C3D4UL
#define ALM_OFFSET  29

/* Flash记录。 */
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
} alm_rec_t;

/* Flash告警块。 */
typedef struct __attribute__((packed))
{
    uint32_t   magic;
    uint8_t    count;
    alm_rec_t  records[MAX_RECORDS];
    uint32_t   crc32;
} alm_blk_t;

#define ALM_CRC_SZ  ((uint16_t)(sizeof(alm_blk_t) - sizeof(uint32_t)))
#define ALM_BUF_SZ  ((uint16_t)(ALM_OFFSET + sizeof(alm_blk_t)))

/* 告警缓存。 */
static char      g_records[MAX_RECORDS][RECORD_LEN];
static uint8_t   g_count;         /* 有效记录数 */
static uint8_t   g_mode;          /* 01上报/02记录 */
static uint32_t  g_last_ms[2];    /* 去抖时间戳 */
static alm_rec_t g_frec[MAX_RECORDS]; /* 二进制记录 */

/* 初始化告警模块。 */
void alm_init(void)
{
    uint8_t i;

    g_count = 0;
    g_mode  = 0x02U;

    for(i = 0; i < 2; i++) 
	{
        g_last_ms[i] = 0;
    }
}

/* 设置告警模式。 */
void alm_set_mode(uint8_t mode)
{
    if((mode == 0x01U) || (mode == 0x02U)) 
	{
        g_mode = mode;
    }
}

/* 保存告警块。 */
static void save_flash(void)
{
    uint8_t    buf[ALM_BUF_SZ];
    alm_blk_t *blk;
    uint8_t    i;

    if(BOOTLOADER_PORT_STATUS_OK != bootloader_port_read_user_config(buf, ALM_BUF_SZ)) 
	{
        return;
    }

    blk = (alm_blk_t *)&buf[ALM_OFFSET];
    blk->magic = ALM_MAGIC;
    blk->count = g_count;

    for(i = 0; i < g_count; i++) 
	{
        blk->records[i] = g_frec[i];
    }

    blk->crc32 = bootloader_port_crc32_calc((const uint8_t *)blk, ALM_CRC_SZ);

    bootloader_port_write_user_config(buf, ALM_BUF_SZ);
}

/* 追加告警记录，满则丢最旧。 */
static void append_record(const char *text, const alm_rec_t *rec)
{
    uint8_t i;

    if(g_count >= MAX_RECORDS) 
	{
        for(i = 0; i < (MAX_RECORDS - 1); i++) 
		{
            memcpy(g_records[i], g_records[i + 1], RECORD_LEN);
            g_frec[i] = g_frec[i + 1];
        }
        g_count = (uint8_t)(MAX_RECORDS - 1);
    }

    snprintf(g_records[g_count], RECORD_LEN, "%s", text);
    g_frec[g_count] = *rec;
    g_count++;
}

/* 超阈后记录告警，主动模式直接发ASCII。 */
void alm_check(uint8_t channel, float threshold, float value)
{
    uint32_t           now_ms;
    bsp_rtc_datetime_t dt;
    char               text[RECORD_LEN];
    alm_rec_t          rec;

    if(channel > 1) { return; }
    if(value <= threshold) { return; }

    now_ms = timebase_get_ms32();
    if((uint32_t)(now_ms - g_last_ms[channel]) < DEBOUNCE_MS) 
	{
        return;
    }
    g_last_ms[channel] = now_ms;

    if(0 != bsp_rtc_get_datetime(&dt)) 
	{
        memset(&dt, 0, sizeof(dt));
    }

    snprintf(text, sizeof(text), "%04u-%02u-%02u %02u:%02u:%02u | CH%u | %.2f | %.2f\n", (unsigned)dt.year, (unsigned)dt.month, (unsigned)dt.date, (unsigned)dt.hour, (unsigned)dt.minute, (unsigned)dt.second,
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

    if(0x01U == g_mode) 
	{
        bsp_usart_send_buffer(RS485_USART, (const uint8_t *)text, (uint16_t)strlen(text));
    }

    append_record(text, &rec);
}

/* 倒序输出告警记录。 */
void alm_query(char *buf, uint16_t size)
{
    uint16_t pos = 0;
    uint16_t rec_len;
    int8_t   i;

    if(!buf || (0 == size)) { return; }

    if(0 == g_count) 
	{
        snprintf(buf, size, "empty");
        return;
    }

    for(i = (int8_t)(g_count - 1); i >= 0; i--) 
	{
        rec_len = (uint16_t)strlen(g_records[i]);
        if((pos + rec_len) >= size) { break; }
        memcpy(&buf[pos], g_records[i], rec_len);
        pos += rec_len;
    }
    buf[pos] = '\0';
}

/* 清空记录并重置去抖。 */
void alm_clear(void)
{
    uint8_t  i;
    uint32_t now = timebase_get_ms32();

    g_count = 0;

    for(i = 0; i < 2; i++)
    {
        g_last_ms[i] = now;
    }
}

/* 保存当前记录到Flash。 */
void alm_save(void)
{
    save_flash();
}

/* 从Flash恢复记录。 */
void alm_load(void)
{
    uint8_t             buf[ALM_BUF_SZ];
    const alm_blk_t    *blk;
    uint32_t            crc;
    uint8_t             i;
    bsp_rtc_datetime_t  dt;
    char                text[RECORD_LEN];

    if(BOOTLOADER_PORT_STATUS_OK != bootloader_port_read_user_config(buf, ALM_BUF_SZ)) 
	{
        return;
    }

    blk = (const alm_blk_t *)&buf[ALM_OFFSET];

    if(blk->magic != ALM_MAGIC) { return; }

    crc = bootloader_port_crc32_calc(
        (const uint8_t *)blk, ALM_CRC_SZ);
    if(crc != blk->crc32) { return; }

    if(blk->count > MAX_RECORDS) { return; }

    g_count = blk->count;

    for(i = 0; i < g_count; i++) 
	{
        g_frec[i] = blk->records[i];

        dt.year   = blk->records[i].year;
        dt.month  = blk->records[i].month;
        dt.date   = blk->records[i].date;
        dt.hour   = blk->records[i].hour;
        dt.minute = blk->records[i].minute;
        dt.second = blk->records[i].second;

        snprintf(text, sizeof(text), "%04u-%02u-%02u %02u:%02u:%02u | CH%u | %.2f | %.2f\n", (unsigned)dt.year, (unsigned)dt.month, (unsigned)dt.date, (unsigned)dt.hour, (unsigned)dt.minute, (unsigned)dt.second,
                       (unsigned)blk->records[i].channel, (double)blk->records[i].threshold, (double)blk->records[i].value);

        snprintf(g_records[i], RECORD_LEN, "%s", text);
    }
}
