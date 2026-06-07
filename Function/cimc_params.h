#ifndef CIMC_PARAMS_H
#define CIMC_PARAMS_H

/*
 * 文件作用：
 *   定义 App 层所有需要跨重启持久化的运行参数结构体和读写接口。
 *   参数存储在 BootLoader 参数区的 user_config 段（512 字节），重启后仍有效。
 *   底层 Flash 擦写通过 bootloader_port 提供的读写接口完成，本模块不直接操作 Flash。
 */

#define SYSTEM_ALL_BASE_ONLY
#include "system_all.h"
#undef SYSTEM_ALL_BASE_ONLY

#ifdef __cplusplus
extern "C" {
#endif

/* 波特率映射码：11=4800, 12=9600, 13=19200, 14=115200 */
#define CIMC_PARAMS_BAUD_CODE_4800    0x11U
#define CIMC_PARAMS_BAUD_CODE_9600    0x12U
#define CIMC_PARAMS_BAUD_CODE_19200   0x13U
#define CIMC_PARAMS_BAUD_CODE_115200  0x14U

/* 自动上报间隔映射码（0x0261命令）：01=1s, 02=3s, 03=5s */
#define CIMC_PARAMS_INTERVAL_1S       0x01U
#define CIMC_PARAMS_INTERVAL_3S       0x02U
#define CIMC_PARAMS_INTERVAL_5S       0x03U

/* 告警模式：01=主动上报（超阈后立即发送），02=仅记录（查询后返回）。 */
#define CIMC_PARAMS_ALARM_MODE_ACTIVE  0x01U
#define CIMC_PARAMS_ALARM_MODE_RECORD  0x02U

/* 设备 ID 有效范围；0x0000 保留禁用，0xFFFF 为广播地址，均不允许写入本机 ID。 */
#define CIMC_PARAMS_DEVICE_ID_MIN      0x0001U
#define CIMC_PARAMS_DEVICE_ID_MAX      0xFFFEU
#define CIMC_PARAMS_DEVICE_ID_DEFAULT  0x0001U

/*
 * 结构体作用：
 *   保存所有需要跨重启持久化的 App 运行参数。
 *   以 packed 方式写入 Flash user_config 区，最后的 crc32 字段
 *   覆盖前面所有字段，用于验证每次上电后读取的数据完整性。
 * 成员说明：
 *   magic：固定标识字，用于判断 user_config 区是否已写入合法 App 参数。
 *   device_id：本机设备 ID，范围 0x0001 ~ 0xFFFE。
 *   baud_code：波特率映射码，见 CIMC_PARAMS_BAUD_CODE_* 宏。
 *   ch0_ratio：CH0（电位器）数据变比；查询/上报值 = 原始采样值 × ch0_ratio，默认 1.0。
 *   ch1_ratio：CH1（DAC 回读）数据变比，默认 1.0。
 *   ch0_threshold：CH0 超限告警阈值，单位与 CH0 变比后的上报值一致。
 *   ch1_threshold：CH1 超限告警阈值。
 *   report_interval：定时上报间隔码，见 CIMC_PARAMS_INTERVAL_* 宏。
 *   alarm_mode：告警模式，见 CIMC_PARAMS_ALARM_MODE_* 宏。
 *   crc32：对 magic ~ alarm_mode 所有字段计算的 CRC32，保护参数完整性。
 */
typedef struct __attribute__((packed))
{
    uint32_t magic;
    uint16_t device_id;
    uint8_t  baud_code;
    float    ch0_ratio;
    float    ch1_ratio;
    float    ch0_threshold;
    float    ch1_threshold;
    uint8_t  report_interval;
    uint8_t  alarm_mode;
    uint32_t crc32;
} cimc_params_t;

/*
 * 函数作用：
 *   从 Flash user_config 区加载参数到 RAM。
 *   若魔术字不匹配或 CRC32 校验失败，则填充出厂默认值并立即持久化，
 *   确保下次上电能直接读到合法数据而不再走初始化路径。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   无返回值。
 */
void cimc_params_load(void);

/*
 * 函数作用：
 *   将当前 RAM 参数重新计算 CRC32 后写回 Flash user_config 区。
 *   底层执行整页读-改-写，不影响参数区其他字段（如升级控制、设备信息）。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   无返回值。
 */
void cimc_params_save(void);

/*
 * 函数作用：
 *   获取当前 RAM 中参数结构体的只读指针，供协议层直接读取参数字段。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   返回指向全局参数结构体的常量指针；调用方不得通过此指针修改数据，
 *   需要修改参数时应调用对应的 cimc_params_set_* 函数。
 */
const cimc_params_t *cimc_params_get(void);

/*
 * 函数作用：
 *   将当前存储的波特率映射码转换为 USART 初始化所需的实际波特率数值。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   返回实际波特率（4800/9600/19200/115200）；
 *   若映射码异常（Flash 数据损坏等），安全回退到出厂默认值 19200。
 */
uint32_t cimc_params_get_baud_rate(void);

/*
 * 函数作用：
 *   验证 id 合法后更新设备 ID 并立即持久化到 Flash。
 * 参数说明：
 *   id：新设备 ID，有效范围 0x0001 ~ 0xFFFE。
 * 返回值说明：
 *   1：id 合法，已写入 RAM 和 Flash。
 *   0：id 超出有效范围，参数未修改。
 */
uint8_t cimc_params_set_device_id(uint16_t id);

/*
 * 函数作用：
 *   验证 code 合法后更新波特率映射码并立即持久化到 Flash。
 *   注意：此函数只持久化映射码，实际切换 USART 波特率需要调用方另行处理。
 * 参数说明：
 *   code：波特率映射码，见 CIMC_PARAMS_BAUD_CODE_* 宏（0x11/0x12/0x13/0x14）。
 * 返回值说明：
 *   1：code 合法，已写入 RAM 和 Flash。
 *   0：code 非法，参数未修改。
 */
uint8_t cimc_params_set_baud_code(uint8_t code);

/*
 * 函数作用：
 *   更新 CH0 变比并立即持久化到 Flash。
 * 参数说明：
 *   ratio：新变比值；任意 float 均合法，由调用方保证业务含义合理。
 * 返回值说明：
 *   无返回值。
 */
void cimc_params_set_ch0_ratio(float ratio);

/*
 * 函数作用：
 *   更新 CH1 变比并立即持久化到 Flash。
 * 参数说明：
 *   ratio：新变比值。
 * 返回值说明：
 *   无返回值。
 */
void cimc_params_set_ch1_ratio(float ratio);

/*
 * 函数作用：
 *   更新 CH0 告警阈值并立即持久化到 Flash。
 * 参数说明：
 *   threshold：新阈值，单位与 CH0 变比后的上报值一致。
 * 返回值说明：
 *   无返回值。
 */
void cimc_params_set_ch0_threshold(float threshold);

/*
 * 函数作用：
 *   更新 CH1 告警阈值并立即持久化到 Flash。
 * 参数说明：
 *   threshold：新阈值。
 * 返回值说明：
 *   无返回值。
 */
void cimc_params_set_ch1_threshold(float threshold);

/*
 * 函数作用：
 *   验证 interval 合法后更新自动上报间隔码并立即持久化到 Flash。
 * 参数说明：
 *   interval：间隔映射码，见 CIMC_PARAMS_INTERVAL_* 宏（0x01/0x02/0x03）。
 * 返回值说明：
 *   1：interval 合法，已写入 RAM 和 Flash。
 *   0：interval 非法，参数未修改。
 */
uint8_t cimc_params_set_report_interval(uint8_t interval);

/*
 * 函数作用：
 *   验证 mode 合法后更新告警模式并立即持久化到 Flash。
 * 参数说明：
 *   mode：告警模式码，见 CIMC_PARAMS_ALARM_MODE_* 宏（0x01/0x02）。
 * 返回值说明：
 *   1：mode 合法，已写入 RAM 和 Flash。
 *   0：mode 非法，参数未修改。
 */
uint8_t cimc_params_set_alarm_mode(uint8_t mode);

#ifdef __cplusplus
}
#endif

#endif /* CIMC_PARAMS_H */
