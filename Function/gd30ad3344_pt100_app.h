#ifndef __GD30AD3344_PT100_APP_H_
#define __GD30AD3344_PT100_APP_H_

#include "system_all.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * 结构体作用：
 *   保存一次 GD30AD3344 + PT100 前端采样链路换算后的应用层结果。
 * 成员说明：
 *   sample_ready：为 1 表示已经完成至少一次非丢弃采样，缓存数据可被显示或上传。
 *   range_valid：为 1 表示温度落在本应用支持的 -50℃~150℃ 范围内。
 *   adc_voltage_v：GD30AD3344 采集到的商业版 PT100 调理模块 Vout，单位 V。
 *   pt100_voltage_v：Vout 扣除 0.9617V 偏置后的有效信号电压，单位 V。
 *   resistance_ohm：按 R测=(Vout-0.9617)/0.001957 反算出的 PT100 电阻，单位 Ω。
 *   temperature_c：按 T≈2.635*R测-263.5 换算并限幅后的温度，单位 ℃。
 */
typedef struct {
    uint8_t sample_ready;
    uint8_t range_valid;
    float adc_voltage_v;
    float pt100_voltage_v;
    float resistance_ohm;
    float temperature_c;
} pt100_measurement_t;

/*
 * 函数作用：
 *   初始化 GD30AD3344 PT100 应用层缓存和首帧丢弃状态。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   无返回值。
 */
void gd30ad3344_pt100_app_init(void);

/*
 * 函数作用：
 *   周期性读取 GD30AD3344 的 AIN0~GND 电压，并换算成 PT100 电阻和温度。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   无返回值。
 */
void gd30ad3344_pt100_task(void);

/*
 * 函数作用：
 *   获取最近一次 PT100 采样和温度换算结果。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   返回测量结果结构体副本；如果 sample_ready 为 0，表示当前尚无可用采样。
 */
pt100_measurement_t gd30ad3344_pt100_get_latest(void);

#ifdef __cplusplus
}
#endif

#endif /* __GD30AD3344_PT100_APP_H_ */
