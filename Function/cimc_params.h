#ifndef CIMC_PARAMS_H
#define CIMC_PARAMS_H

/* 持久化 App 运行参数的结构体定义与 Flash 读写接口（存储于 BootLoader user_config 区）*/

#define SYSTEM_ALL_BASE_ONLY
#include "system_all.h"
#undef SYSTEM_ALL_BASE_ONLY

#ifdef __cplusplus
extern "C" {
#endif

/* 波特率映射码：11=4800, 12=9600, 13=19200, 14=115200 */
#define BAUD_4800    0x11U
#define BAUD_9600    0x12U
#define BAUD_19200   0x13U
#define BAUD_115200  0x14U

/* 自动上报间隔映射码（0x0261命令）：01=1s, 02=3s, 03=5s */
#define INTV_1S       0x01U
#define INTV_3S       0x02U
#define INTV_5S       0x03U

/* 告警模式：01=主动上报（超阈后立即发送），02=仅记录（查询后返回）。 */
#define ALM_ACTIVE  0x01U
#define ALM_RECORD  0x02U

/* 设备 ID 有效范围；0x0000 保留禁用，0xFFFF 为广播地址，均不允许写入本机 ID。 */
#define ID_MIN      0x0001U
#define ID_MAX      0xFFFEU
#define ID_DEFAULT  0x0001U

/* 持久化参数结构体，packed 方式存入 Flash user_config 区，末尾 crc32 保护完整性 */
typedef struct __attribute__((packed))
{
    uint32_t magic;            // 合法性标识字，判断 user_config 区是否已写入 App 参数
    uint16_t device_id;        // 本机设备 ID，范围 0x0001~0xFFFE
    uint8_t  baud_code;        // 波特率映射码，见 BAUD_* 宏
    float    ch0_ratio;        // CH0（电位器）数据变比；上报值 = 采样值 × ch0_ratio，默认 1.0
    float    ch1_ratio;        // CH1（DAC 回读）数据变比，默认 1.0
    float    ch0_threshold;    // CH0 超限告警阈值，单位与变比后上报值一致
    float    ch1_threshold;    // CH1 超限告警阈值
    uint8_t  report_interval;  // 定时上报间隔码，见 INTV_* 宏
    uint8_t  alarm_mode;       // 告警模式，见 ALM_* 宏
    uint32_t crc32;            // 对 magic~alarm_mode 所有字段计算的 CRC32
} params_t;

/* 从 Flash user_config 区加载参数；魔术字或 CRC32 校验失败时填充默认值并持久化 */
void params_load(void);

/* 重新计算 CRC32 后将当前 RAM 参数写回 Flash user_config 区（整页读-改-写） */
void params_save(void);

/* 返回全局参数结构体的只读指针，调用方不得通过此指针修改数据 */
const params_t *params_get(void);

/* 将波特率映射码转换为实际波特率值（4800/9600/19200/115200）；映射码异常时回退到 19200 */
uint32_t params_baud(void);

/* 设置设备 ID（0x0001~0xFFFE），合法时写入 RAM 和 Flash，返回 1；非法返回 0 */
uint8_t params_set_id(uint16_t id);

/* 设置波特率映射码（0x11~0x14），合法时持久化，返回 1；非法返回 0；实际切换由调用方处理 */
uint8_t params_set_baud(uint8_t code);

/* 更新 CH0 变比并立即持久化到 Flash（任意 float 合法，业务合理性由调用方保证） */
void params_set_r0(float ratio);

/* 更新 CH1 变比并立即持久化到 Flash */
void params_set_r1(float ratio);

/* 更新 CH0 告警阈值并立即持久化到 Flash */
void params_set_thr0(float threshold);

/* 更新 CH1 告警阈值并立即持久化到 Flash */
void params_set_thr1(float threshold);

/* 设置自动上报间隔码（0x01~0x03），合法时持久化，返回 1；非法返回 0 */
uint8_t params_set_intv(uint8_t interval);

/* 设置告警模式码（0x01/0x02），合法时持久化，返回 1；非法返回 0 */
uint8_t params_set_alm(uint8_t mode);

#ifdef __cplusplus
}
#endif

#endif /* CIMC_PARAMS_H */
