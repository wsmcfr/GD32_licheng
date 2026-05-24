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
 *   range_valid：为 1 表示电阻落在本应用支持的 -50℃~150℃ PT100 换算范围内。
 *   adc_voltage_v：GD30AD3344 采集到的前端放大后电压，单位 V。
 *   pt100_voltage_v：反推到 PT100 电阻两端的原始电压，单位 V。
 *   resistance_ohm：根据 1mA 激励电流反算出的 PT100 电阻，单位 Ω。
 *   temperature_c：根据 PT100 Callendar-Van Dusen 模型换算出的温度，单位 ℃。
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
