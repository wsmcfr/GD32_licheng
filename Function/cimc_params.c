#include "cimc_params.h"

/*
 * 魔术字：用于区分"user_config 从未被 App 写入过（全 0xFF）"
 * 和"user_config 已初始化但 CRC 损坏"两种不同的异常情况。
 */
#define CIMC_PARAMS_MAGIC  0xC1C2A5B6UL

/*
 * CRC32 覆盖范围：magic ~ alarm_mode，不含末尾的 crc32 字段本身。
 * 由于结构体为 packed 布局，减去最后 4 字节即为有效计算范围。
 */
#define CIMC_PARAMS_CRC_SIZE  ((uint16_t)(sizeof(cimc_params_t) - sizeof(uint32_t)))

/* 运行时参数 RAM 镜像，模块内部所有读写均通过此全局变量进行。 */
static cimc_params_t g_params;

/*
 * 函数作用：
 *   将全局参数结构体填充为出厂默认值；只写 RAM，不写 Flash。
 *   阈值默认设为 9999.0，远高于正常采样范围（电位器/DAC 回读约 0~5V 对应约 0~5），
 *   避免首次上电未经上位机配置就误触发告警记录。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   无返回值。
 */
static void prv_cimc_params_fill_default(void)
{
    g_params.magic           = CIMC_PARAMS_MAGIC;
    g_params.device_id       = CIMC_PARAMS_DEVICE_ID_DEFAULT;
    g_params.baud_code       = CIMC_PARAMS_BAUD_CODE_19200;
    g_params.ch0_ratio       = 1.0f;
    g_params.ch1_ratio       = 1.0f;
    g_params.ch0_threshold   = 9999.0f;
    g_params.ch1_threshold   = 9999.0f;
    g_params.report_interval = CIMC_PARAMS_INTERVAL_1S;
    g_params.alarm_mode      = CIMC_PARAMS_ALARM_MODE_RECORD;

    /* 默认值填充完毕后同步更新 CRC32，使结构体始终处于自洽状态。 */
    g_params.crc32 = bootloader_port_crc32_calc(
                         (const uint8_t *)&g_params,
                         CIMC_PARAMS_CRC_SIZE);
}

/*
 * 函数作用：
 *   对指定参数结构体重新计算 CRC32 并与结构体内存储的 crc32 字段比较。
 * 参数说明：
 *   params：待校验的参数结构体指针。
 * 返回值说明：
 *   1：计算值与存储值一致，数据完整可信。
 *   0：params 为空或 CRC32 不一致，数据已损坏。
 */
static uint8_t prv_cimc_params_verify_crc(const cimc_params_t *params)
{
    uint32_t calculated;

    if(NULL == params) {
        return 0U;
    }

    calculated = bootloader_port_crc32_calc((const uint8_t *)params,
                                            CIMC_PARAMS_CRC_SIZE);

    return (calculated == params->crc32) ? 1U : 0U;
}

/*
 * 函数作用：
 *   从 Flash user_config 区加载参数到 RAM，并对魔术字和 CRC32 进行双重校验。
 *   读取失败或校验不通过时，用默认值填充 RAM 并立即持久化到 Flash，
 *   确保下次上电能直接读到合法数据而无需再次走初始化路径。
 *   读取操作本身失败（接口返回错误）时不写 Flash，避免在驱动异常时雪上加霜。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   无返回值。
 */
void cimc_params_load(void)
{
    cimc_params_t loaded;

    if(BOOTLOADER_PORT_STATUS_OK != bootloader_port_read_user_config(
        (uint8_t *)&loaded, (uint16_t)sizeof(loaded))) {
        /*
         * 读取接口失败通常说明驱动层有问题，此时只用默认值初始化 RAM，
         * 不尝试回写 Flash，避免在异常状态下破坏参数区其他字段。
         */
        prv_cimc_params_fill_default();
        return;
    }

    if((CIMC_PARAMS_MAGIC != loaded.magic) ||
       (0U == prv_cimc_params_verify_crc(&loaded))) {
        /*
         * 魔术字不匹配说明 user_config 从未被 App 写入（Flash 出厂全 0xFF）；
         * CRC 不匹配说明参数在某次断电或擦写中被部分损坏。
         * 两种情况都需要用默认值初始化并立即持久化，保证后续上电能正常加载。
         */
        prv_cimc_params_fill_default();
        cimc_params_save();
        return;
    }

    g_params = loaded;
}

/*
 * 函数作用：
 *   将当前 RAM 参数重新计算 CRC32 后写回 Flash user_config 区。
 *   底层执行整页读-改-写，不影响参数区其他字段（BootLoader 升级控制字、设备信息等）。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   无返回值。
 */
void cimc_params_save(void)
{
    /* 每次保存前重新计算 CRC32，确保任意字段修改后存储值始终有效。 */
    g_params.crc32 = bootloader_port_crc32_calc(
                         (const uint8_t *)&g_params,
                         CIMC_PARAMS_CRC_SIZE);

    (void)bootloader_port_write_user_config(
        (const uint8_t *)&g_params,
        (uint16_t)sizeof(g_params));
}

/*
 * 函数作用：
 *   获取当前 RAM 中参数结构体的只读指针。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   返回指向全局参数结构体的常量指针；调用方不得通过此指针修改参数，
 *   需要修改时应调用对应的 cimc_params_set_* 函数。
 */
const cimc_params_t *cimc_params_get(void)
{
    return &g_params;
}

/*
 * 函数作用：
 *   将当前存储的波特率映射码转换为 USART 初始化所需的实际波特率数值。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   返回实际波特率（4800/9600/19200/115200）；
 *   映射码异常时安全回退到 19200，防止 USART 初始化使用非法参数。
 */
uint32_t cimc_params_get_baud_rate(void)
{
    switch(g_params.baud_code) {
    case CIMC_PARAMS_BAUD_CODE_4800:
        return 4800U;
    case CIMC_PARAMS_BAUD_CODE_9600:
        return 9600U;
    case CIMC_PARAMS_BAUD_CODE_19200:
        return 19200U;
    case CIMC_PARAMS_BAUD_CODE_115200:
        return 115200U;
    default:
        return 19200U;
    }
}

/*
 * 函数作用：
 *   验证设备 ID 合法性后更新 RAM 并持久化到 Flash。
 * 参数说明：
 *   id：新设备 ID，有效范围 CIMC_PARAMS_DEVICE_ID_MIN ~ CIMC_PARAMS_DEVICE_ID_MAX
 *       （0x0001 ~ 0xFFFE）。
 * 返回值说明：
 *   1：id 合法，已写入 RAM 和 Flash。
 *   0：id 超出有效范围，参数未修改，调用方可据此返回协议错误应答。
 */
uint8_t cimc_params_set_device_id(uint16_t id)
{
    if((id < CIMC_PARAMS_DEVICE_ID_MIN) || (id > CIMC_PARAMS_DEVICE_ID_MAX)) {
        return 0U;
    }

    g_params.device_id = id;
    cimc_params_save();

    return 1U;
}

/*
 * 函数作用：
 *   验证波特率映射码合法性后更新 RAM 并持久化到 Flash。
 *   注意：本函数只持久化映射码，实际切换 USART 波特率需要调用方另行处理
 *   （赛题要求先回 OK，再切换波特率，因此切换动作不在本函数内完成）。
 * 参数说明：
 *   code：波特率映射码，有效值为 0x11/0x12/0x13/0x14。
 * 返回值说明：
 *   1：code 合法，已写入 RAM 和 Flash。
 *   0：code 非法，参数未修改。
 */
uint8_t cimc_params_set_baud_code(uint8_t code)
{
    if((code != CIMC_PARAMS_BAUD_CODE_4800) &&
       (code != CIMC_PARAMS_BAUD_CODE_9600) &&
       (code != CIMC_PARAMS_BAUD_CODE_19200) &&
       (code != CIMC_PARAMS_BAUD_CODE_115200)) {
        return 0U;
    }

    g_params.baud_code = code;
    cimc_params_save();

    return 1U;
}

/*
 * 函数作用：
 *   更新 CH0 变比并持久化到 Flash。任意 float 值均视为合法，由调用方保证业务合理性。
 * 参数说明：
 *   ratio：新变比；查询/上报值 = 原始采样值 × ratio。
 * 返回值说明：
 *   无返回值。
 */
void cimc_params_set_ch0_ratio(float ratio)
{
    g_params.ch0_ratio = ratio;
    cimc_params_save();
}

/*
 * 函数作用：
 *   更新 CH1 变比并持久化到 Flash。
 * 参数说明：
 *   ratio：新变比。
 * 返回值说明：
 *   无返回值。
 */
void cimc_params_set_ch1_ratio(float ratio)
{
    g_params.ch1_ratio = ratio;
    cimc_params_save();
}

/*
 * 函数作用：
 *   更新 CH0 告警阈值并持久化到 Flash。
 * 参数说明：
 *   threshold：新阈值，单位与 CH0 变比后的上报值一致。
 * 返回值说明：
 *   无返回值。
 */
void cimc_params_set_ch0_threshold(float threshold)
{
    g_params.ch0_threshold = threshold;
    cimc_params_save();
}

/*
 * 函数作用：
 *   更新 CH1 告警阈值并持久化到 Flash。
 * 参数说明：
 *   threshold：新阈值。
 * 返回值说明：
 *   无返回值。
 */
void cimc_params_set_ch1_threshold(float threshold)
{
    g_params.ch1_threshold = threshold;
    cimc_params_save();
}

/*
 * 函数作用：
 *   验证间隔码合法性后更新自动上报间隔并持久化到 Flash。
 * 参数说明：
 *   interval：间隔映射码，有效值为 0x01（1s）/0x02（3s）/0x03（5s）。
 * 返回值说明：
 *   1：interval 合法，已写入 RAM 和 Flash。
 *   0：interval 非法，参数未修改。
 */
uint8_t cimc_params_set_report_interval(uint8_t interval)
{
    if((interval != CIMC_PARAMS_INTERVAL_1S) &&
       (interval != CIMC_PARAMS_INTERVAL_3S) &&
       (interval != CIMC_PARAMS_INTERVAL_5S)) {
        return 0U;
    }

    g_params.report_interval = interval;
    cimc_params_save();

    return 1U;
}

/*
 * 函数作用：
 *   验证告警模式码合法性后更新告警模式并持久化到 Flash。
 * 参数说明：
 *   mode：告警模式码，有效值为 0x01（主动上报）/ 0x02（仅记录）。
 * 返回值说明：
 *   1：mode 合法，已写入 RAM 和 Flash。
 *   0：mode 非法，参数未修改。
 */
uint8_t cimc_params_set_alarm_mode(uint8_t mode)
{
    if((mode != CIMC_PARAMS_ALARM_MODE_ACTIVE) &&
       (mode != CIMC_PARAMS_ALARM_MODE_RECORD)) {
        return 0U;
    }

    g_params.alarm_mode = mode;
    cimc_params_save();

    return 1U;
}
