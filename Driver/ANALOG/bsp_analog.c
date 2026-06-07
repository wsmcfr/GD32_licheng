#include "bsp_analog.h"

__IO uint16_t adc_value[2];
uint16_t convertarr[CONVERT_NUM] = {0};

/* TIMER5 作为 DAC 触发源，10kHz 更新频率 */
static void timer5_config(void)
{
    timer_parameter_struct timer_initpara;

    timer_deinit(TIMER5);
    timer_struct_para_init(&timer_initpara);
    timer_initpara.prescaler = 239;
    timer_initpara.alignedmode = TIMER_COUNTER_EDGE;
    timer_initpara.counterdirection = TIMER_COUNTER_UP;
    timer_initpara.period = 99;
    timer_initpara.clockdivision = TIMER_CKDIV_DIV1;
    timer_initpara.repetitioncounter = 0;
    timer_init(TIMER5, &timer_initpara);
    timer_master_output_trigger_source_select(TIMER5, TIMER_TRI_OUT_SRC_UPDATE);
    timer_enable(TIMER5);
}

/* ADC0 双通道扫描 + DMA 循环，PC0=CH0电位器，PC1=CH1 DAC回读 */
void bsp_adc_init(void)
{
    dma_single_data_parameter_struct dma_single_data_parameter;

    rcu_periph_clock_enable(ADC1_CLK_PORT);
    rcu_periph_clock_enable(RCU_ADC0);
    rcu_periph_clock_enable(RCU_DMA1);

    adc_clock_config(ADC_ADCCK_PCLK2_DIV8);

    gpio_mode_set(ADC1_PORT, GPIO_MODE_ANALOG, GPIO_PUPD_NONE,ADC1_PIN | ADC2_PIN | ADC_VREF_PIN);

    dma_deinit(DMA1, DMA_CH0);
    dma_single_data_para_struct_init(&dma_single_data_parameter);
    dma_single_data_parameter.periph_addr = (uint32_t)(&ADC_RDATA(ADC0));
    dma_single_data_parameter.periph_inc = DMA_PERIPH_INCREASE_DISABLE;
    dma_single_data_parameter.memory0_addr = (uint32_t)adc_value;
    dma_single_data_parameter.memory_inc = DMA_MEMORY_INCREASE_ENABLE;
    dma_single_data_parameter.periph_memory_width = DMA_PERIPH_WIDTH_16BIT;
    dma_single_data_parameter.direction = DMA_PERIPH_TO_MEMORY;
    dma_single_data_parameter.number = 2;
    dma_single_data_parameter.priority = DMA_PRIORITY_HIGH;
    dma_single_data_mode_init(DMA1, DMA_CH0, &dma_single_data_parameter);
    dma_channel_subperipheral_select(DMA1, DMA_CH0, DMA_SUBPERI0);

    dma_circulation_enable(DMA1, DMA_CH0);
    dma_channel_enable(DMA1, DMA_CH0);

    adc_sync_mode_config(ADC_SYNC_MODE_INDEPENDENT);
    adc_special_function_config(ADC0, ADC_CONTINUOUS_MODE, ENABLE);
    adc_special_function_config(ADC0, ADC_SCAN_MODE, ENABLE);
    adc_data_alignment_config(ADC0, ADC_DATAALIGN_RIGHT);

    adc_channel_length_config(ADC0, ADC_ROUTINE_CHANNEL, 2);
    adc_routine_channel_config(ADC0, 0, ADC_CHANNEL_10, ADC_SAMPLETIME_15);
    adc_routine_channel_config(ADC0, 1, ADC_CHANNEL_11, ADC_SAMPLETIME_15);
    adc_external_trigger_source_config(ADC0, ADC_ROUTINE_CHANNEL, ADC_EXTTRIG_ROUTINE_T0_CH0);
    adc_external_trigger_config(ADC0, ADC_ROUTINE_CHANNEL, EXTERNAL_TRIGGER_DISABLE);

    adc_dma_request_after_last_enable(ADC0);
    adc_dma_mode_enable(ADC0);

    adc_enable(ADC0);
    delay_1ms(1);
    adc_calibration_enable(ADC0);

    adc_software_trigger_enable(ADC0, ADC_ROUTINE_CHANNEL);
}

/* DAC0 CH0，PA4模拟输出，TIMER5 TRGO触发 */
void bsp_dac_init(void)
{
    rcu_periph_clock_enable(DAC1_CLK_PORT);
    rcu_periph_clock_enable(RCU_DAC);
    rcu_periph_clock_enable(RCU_TIMER5);

    gpio_mode_set(DAC1_PORT, GPIO_MODE_ANALOG, GPIO_PUPD_NONE, DAC1_PIN);

    dac_deinit(DAC0);
    dac_trigger_source_config(DAC0, DAC_OUT0, DAC_TRIGGER_T5_TRGO);
    dac_trigger_enable(DAC0, DAC_OUT0);
    dac_wave_mode_config(DAC0, DAC_OUT0, DAC_WAVE_DISABLE);
    dac_enable(DAC0, DAC_OUT0);

    timer5_config();
}
