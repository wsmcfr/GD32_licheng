#include "gd30ad3344_pt100_app.h"

#define PT100_ADC_CHANNEL    GD30AD3344_Channel_4
#define PT100_ADC_PGA        GD30AD3344_PGA_4V096

/* 商业版PT100模块两点标定参数：100Ω->1.1355V，154Ω->1.2398V */
#define PT100_OFFSET_V       0.94235185f
#define PT100_SLOPE_V_OHM    0.0019314815f

#define PT100_MIN_C          (-50.0f)
#define PT100_MAX_C          150.0f

#define PT100_TABLE_COUNT    (sizeof(s_pt100_table) / sizeof(s_pt100_table[0]))

typedef struct 
{
    float resistance_ohm;
    float temperature_c;
} pt100_point_t;

/* 测试板电阻-温度标定表，按电阻升序排列，用于分段线性插值 */
static const pt100_point_t s_pt100_table[] = 
{
    {80.6f,  -49.27f},
    {82.5f,  -44.49f},
    {100.0f,   0.0f},
    {107.79f, 20.0f},
    {113.0f,  33.44f},
    {115.0f,  38.61f},
    {115.54f, 40.0f},
    {123.24f, 60.0f},
    {130.90f, 80.0f},
    {138.51f, 100.0f},
    {150.0f,  130.45f},
    {154.0f,  141.11f},
};

static pt100_measurement_t s_latest;
static uint8_t s_discard = 1; /* 上电首帧丢弃，等MUX/PGA稳定 */

/* 根据测试板标定表分段线性插值，电阻→温度 */
static float resistance_to_temp(float r)
{
    uint32_t i;

    if(r <= s_pt100_table[0].resistance_ohm)
        return s_pt100_table[0].temperature_c;

    for(i = 1; i < PT100_TABLE_COUNT; i++) 
	{
        if(r <= s_pt100_table[i].resistance_ohm) 
		{
            float span = s_pt100_table[i].resistance_ohm - s_pt100_table[i-1].resistance_ohm;
            float pos  = (r - s_pt100_table[i-1].resistance_ohm) / span;
            return s_pt100_table[i-1].temperature_c + pos * (s_pt100_table[i].temperature_c - s_pt100_table[i-1].temperature_c);
        }
    }

    return s_pt100_table[PT100_TABLE_COUNT - 1].temperature_c;
}

static float clamp_temp(float t, uint8_t *valid)
{
    if(valid) *valid = 1;
    if(t <= PT100_MIN_C) { if(valid) *valid = 0; return PT100_MIN_C; }
    if(t >= PT100_MAX_C) { if(valid) *valid = 0; return PT100_MAX_C; }
    return t;
}

void gd30ad3344_pt100_app_init(void)
{
    memset(&s_latest, 0, sizeof(s_latest));
    s_discard = 1;
}

/* 200ms周期：读ADC电压→反算电阻→插值温度 */
void gd30ad3344_pt100_task(void)
{
    float adc_v, sig_v, r, t;
    uint8_t valid = 0;

    if(GD30AD3344_AD_Read(PT100_ADC_CHANNEL, PT100_ADC_PGA, &adc_v) != 0) 
	{
        s_latest.sample_ready = 0;
        s_latest.range_valid  = 0;
        return;
    }

    if(s_discard) 
	{
        s_discard = 0;
        return;
    }

    sig_v = adc_v - PT100_OFFSET_V;
    r     = sig_v / PT100_SLOPE_V_OHM;
    t     = resistance_to_temp(r);

    s_latest.adc_voltage_v   = adc_v;
    s_latest.pt100_voltage_v = sig_v;
    s_latest.resistance_ohm  = r;
    s_latest.temperature_c   = clamp_temp(t, &valid);
    s_latest.range_valid     = valid;
    s_latest.sample_ready    = 1;
}

pt100_measurement_t gd30ad3344_pt100_get_latest(void)
{
    return s_latest;
}
