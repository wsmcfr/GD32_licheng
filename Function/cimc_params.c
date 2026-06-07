#include "cimc_params.h"

/* user_config区初始化标志：区分"从未写入（全0xFF）"与"CRC损坏" */
#define PARAMS_MAGIC  0xC1C2A5B6UL

/* CRC32覆盖范围：magic~alarm_mode，不含末尾crc32字段 */
#define PARAMS_CRC_SIZE  ((uint16_t)(sizeof(params_t) - sizeof(uint32_t)))

static params_t g_params; /* 运行时参数RAM镜像 */

/* 用出厂默认值填充g_params，只写RAM；阈值默认9999.0防止首次上电误触告警 */
static void fill_default(void)
{
    g_params.magic           = PARAMS_MAGIC;
    g_params.device_id       = ID_DEFAULT;
    g_params.baud_code       = BAUD_19200;
    g_params.ch0_ratio       = 1.0f;
    g_params.ch1_ratio       = 1.0f;
    g_params.ch0_threshold   = 9999.0f;
    g_params.ch1_threshold   = 9999.0f;
    g_params.report_interval = INTV_1S;
    g_params.alarm_mode      = ALM_RECORD;

    g_params.crc32 = bootloader_port_crc32_calc((const uint8_t *)&g_params, PARAMS_CRC_SIZE);
}

/* 重新计算CRC32并与结构体存储值比较，不一致返回0 */
static uint8_t verify_crc(const params_t *p)
{
    uint32_t calc;

    if(!p) { return 0; }

    calc = bootloader_port_crc32_calc((const uint8_t *)p, PARAMS_CRC_SIZE);
    return (calc == p->crc32) ? 1 : 0;
}

/*
 * 从Flash user_config区加载参数，魔术字+CRC32双重校验。
 * 校验失败时用默认值填充并持久化；读接口失败时只填充RAM，不回写Flash。
 */
void params_load(void)
{
    params_t loaded;

    if(BOOTLOADER_PORT_STATUS_OK != bootloader_port_read_user_config((uint8_t *)&loaded, (uint16_t)sizeof(loaded))) 
	{
        fill_default();
        return;
    }

    if((PARAMS_MAGIC != loaded.magic) || (0 == verify_crc(&loaded))) 
	{
        fill_default();
        params_save();
        return;
    }

    g_params = loaded;
}

/* 重新计算CRC32后将g_params写回Flash，底层执行整页读-改-写 */
void params_save(void)
{
    g_params.crc32 = bootloader_port_crc32_calc((const uint8_t *)&g_params, PARAMS_CRC_SIZE);

    bootloader_port_write_user_config((const uint8_t *)&g_params, (uint16_t)sizeof(g_params));
}

/* 获取当前RAM参数结构体只读指针，修改时调用对应set_*函数 */
const params_t *params_get(void)
{
    return &g_params;
}

/* 将波特率映射码转换为实际波特率，映射码异常时安全回退到19200 */
uint32_t params_baud(void)
{
    switch(g_params.baud_code) 
	{
		case BAUD_4800:   return 4800;
		case BAUD_9600:   return 9600;
		case BAUD_19200:  return 19200;
		case BAUD_115200: return 115200;
		default:                           return 19200;
    }
}

/* 验证ID有效范围（0x0001~0xFFFE）后更新RAM+Flash，超范围返回0 */
uint8_t params_set_id(uint16_t id)
{
    if((id < ID_MIN) || (id > ID_MAX)) 
	{
        return 0;
    }

    g_params.device_id = id;
    params_save();
    return 1;
}

/*
 * 验证波特率码合法性（0x11/0x12/0x13/0x14）后写RAM+Flash。
 * 只持久化映射码，不切换USART，调用方负责后续切换。
 */
uint8_t params_set_baud(uint8_t code)
{
    if((code != BAUD_4800)  && (code != BAUD_9600)  && (code != BAUD_19200) && (code != BAUD_115200)) 
	{
        return 0;
    }

    g_params.baud_code = code;
    params_save();
    return 1;
}

// CH0变比更新并持久化
void params_set_r0(float ratio)
{
    g_params.ch0_ratio = ratio;
    params_save();
}

// CH1变比更新并持久化
void params_set_r1(float ratio)
{
    g_params.ch1_ratio = ratio;
    params_save();
}

// CH0阈值更新并持久化
void params_set_thr0(float threshold)
{
    g_params.ch0_threshold = threshold;
    params_save();
}

// CH1阈值更新并持久化
void params_set_thr1(float threshold)
{
    g_params.ch1_threshold = threshold;
    params_save();
}

/* 验证间隔码（0x01=1s/0x02=3s/0x03=5s）后更新并持久化 */
uint8_t params_set_intv(uint8_t interval)
{
    if((interval != INTV_1S) && (interval != INTV_3S) && (interval != INTV_5S)) 
	{
        return 0;
    }

    g_params.report_interval = interval;
    params_save();
    return 1;
}

/* 验证告警模式码（0x01=主动/0x02=仅记录）后更新并持久化 */
uint8_t params_set_alm(uint8_t mode)
{
    if((mode != ALM_ACTIVE) && (mode != ALM_RECORD)) 
	{
        return 0;
    }

    g_params.alarm_mode = mode;
    params_save();
    return 1;
}
