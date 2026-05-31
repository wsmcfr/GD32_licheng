/*!
    \file    gd30ad3344.c
    \brief   gd30ad3344 driver
    
    \version 2024-10-08, V1.0.0, firmware for GD30AD3344
*/

#include "gd30ad3344.h"

/*
 * 宏作用：
 *   定义 GD30AD3344 SPI DMA 等待的最大循环次数。
 * 说明：
 *   该值用于异常硬件或时钟未恢复时跳出等待，避免低功耗入口永久卡死。
 */
#define GD30AD3344_SPI_WAIT_TIMEOUT     0x00FFFFFFUL

/* GD30AD3344 DMA 临时发送缓冲区。 */
static uint8_t spi3_send_array[GD30AD3344_DMA_BUFFER_SIZE];

/* GD30AD3344 DMA 临时接收缓冲区。 */
static uint8_t spi3_receive_array[GD30AD3344_DMA_BUFFER_SIZE];

/*
 * 变量作用：
 *   记录最近一次 GD30AD3344 SPI DMA 传输是否超时。
 * 说明：
 *   SPI 同步回读的 0xFFFF 可能是器件数据本身，不能作为唯一失败哨兵。
 *   该状态位用于配置下发和低功耗入口判断真实 DMA 错误。
 */
static uint8_t s_gd30ad3344_dma_error;

/*
 * 函数作用：
 *   提前声明本文件内部会在后文实现的 SPI 半字发送函数，避免 ArmClang 在静态 helper
 *   中先使用、后定义时按“隐式声明”处理并直接报错。
 * 参数说明：
 *   half_word：待通过 SPI3 发送给 GD30AD3344 的 16 位配置字。
 * 返回值说明：
 *   返回器件在同一帧内回传的 16 位数据。
 */
uint16_t spi_gd30ad3344_send_halfword_dma(uint16_t half_word);

/*
 * 函数作用：
 *   等待 GD30AD3344 SPI DMA RX 通道传输完成，并带超时保护。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   0：表示 DMA 已完成。
 *  -1：表示等待超时，调用方应放弃本次读写。
 */
static int prv_gd30ad3344_wait_dma_done(void)
{
    uint32_t timeout = GD30AD3344_SPI_WAIT_TIMEOUT;

    while(RESET == dma_flag_get(DMA1, DMA_CH3, DMA_FLAG_FTF)) {
        if(0U == timeout) {
            dma_flag_clear(DMA1, DMA_CH3, DMA_FLAG_FTF);
            dma_flag_clear(DMA1, DMA_CH4, DMA_FLAG_FTF);
            return -1;
        }
        timeout--;
    }

    dma_flag_clear(DMA1, DMA_CH3, DMA_FLAG_FTF);
    dma_flag_clear(DMA1, DMA_CH4, DMA_FLAG_FTF);
    return 0;
}

/*
 * 函数作用：
 *   将一份配置结构体写入 GD30AD3344 配置寄存器。
 * 参数说明：
 *   config：待下发的配置结构体指针，必须指向有效配置。
 * 返回值说明：
 *   0：表示配置已经成功写入 SPI 总线。
 *  -1：表示参数为空或 SPI DMA 等待超时。
 * 说明：
 *   该器件的低功耗模式依赖 MODE/NOP 等配置位切换，因此把写配置动作独立封装，
 *   便于运行态初始化和深睡前待机复用同一条下发路径。
 */
static int prv_gd30ad3344_apply_config(const GD30AD3344 *config)
{
    uint16_t config_value;
    uint16_t rx_value;

    if(NULL == config) {
        return -1;
    }

    config_value = (uint16_t)((config->SS << 15) |
                              (config->MUX << 12) |
                              (config->PGA << 9) |
                              (config->MODE << 8) |
                              (config->DR << 5) |
                              (config->RESERVED_1 << 4) |
                              (config->PULL_UP_EN << 3) |
                              (config->NOP << 1) |
                              (config->RESERVED << 0));

    spi_enable(SPI_GD30AD3344);
    rx_value = spi_gd30ad3344_send_halfword_dma(config_value);
    (void)rx_value;
    if(0U != s_gd30ad3344_dma_error) {
        return -1;
    }

    return 0;
}

/**
 * @brief 使用 DMA 发送并接收一个字节
 * @param byte 要发送的字节
 * @return 从 SPI 总线接收到的字节
 */
uint8_t spi_gd30ad3344_send_byte_dma(uint8_t byte)
{
    dma_single_data_parameter_struct dma_init_struct;

    s_gd30ad3344_dma_error = 0U;

    /* 将数据放入发送缓冲区 */
    spi3_send_array[0] = byte;
    
    /* 配置 DMA 发送通道 */
    dma_deinit(DMA1, DMA_CH4);
    dma_init_struct.periph_addr         = (uint32_t)&SPI_DATA(SPI_GD30AD3344);
    dma_init_struct.memory0_addr        = (uint32_t)spi3_send_array;
    dma_init_struct.direction           = DMA_MEMORY_TO_PERIPH;
    dma_init_struct.periph_memory_width = DMA_PERIPH_WIDTH_8BIT;
    dma_init_struct.priority            = DMA_PRIORITY_HIGH;
    dma_init_struct.number              = 1; /* 只发送一个字节 */
    dma_init_struct.periph_inc          = DMA_PERIPH_INCREASE_DISABLE;
    dma_init_struct.memory_inc          = DMA_MEMORY_INCREASE_ENABLE;
    dma_init_struct.circular_mode       = DMA_CIRCULAR_MODE_DISABLE;
    dma_single_data_mode_init(DMA1, DMA_CH4, &dma_init_struct);
    dma_channel_subperipheral_select(DMA1, DMA_CH4, DMA_SUBPERI5);
    
    /* 配置 DMA 接收通道 */
    dma_deinit(DMA1, DMA_CH3);
    dma_init_struct.periph_addr         = (uint32_t)&SPI_DATA(SPI_GD30AD3344);
    dma_init_struct.memory0_addr        = (uint32_t)spi3_receive_array;
    dma_init_struct.direction           = DMA_PERIPH_TO_MEMORY;
    dma_init_struct.priority            = DMA_PRIORITY_HIGH;
    dma_single_data_mode_init(DMA1, DMA_CH3, &dma_init_struct);
    dma_channel_subperipheral_select(DMA1, DMA_CH3, DMA_SUBPERI5);
    
    /* 启用接收和发送的 DMA 通道 */
    dma_channel_enable(DMA1, DMA_CH3);
    dma_channel_enable(DMA1, DMA_CH4);
    
    /* 启用 SPI 的 DMA 接收和发送功能 */
    spi_dma_enable(SPI_GD30AD3344, SPI_DMA_RECEIVE);
    spi_dma_enable(SPI_GD30AD3344, SPI_DMA_TRANSMIT);
    
    if(0 != prv_gd30ad3344_wait_dma_done()) {
        spi_dma_disable(SPI_GD30AD3344, SPI_DMA_RECEIVE);
        spi_dma_disable(SPI_GD30AD3344, SPI_DMA_TRANSMIT);
        dma_channel_disable(DMA1, DMA_CH3);
        dma_channel_disable(DMA1, DMA_CH4);
        s_gd30ad3344_dma_error = 1U;
        return 0xFFU;
    }
    
    /* 禁用 DMA */
    spi_dma_disable(SPI_GD30AD3344, SPI_DMA_RECEIVE);
    spi_dma_disable(SPI_GD30AD3344, SPI_DMA_TRANSMIT);
    dma_channel_disable(DMA1, DMA_CH3);
    dma_channel_disable(DMA1, DMA_CH4);
    
    /* 返回接收到的数据 */
    s_gd30ad3344_dma_error = 0U;
    return spi3_receive_array[0];
}

/**
 * @brief 使用 DMA 发送并接收一个半字（16位数据）
 * @param half_word 要发送的半字
 * @return 从 SPI 总线接收到的半字
 */
uint16_t spi_gd30ad3344_send_halfword_dma(uint16_t half_word)
{
    uint16_t rx_data;
    dma_single_data_parameter_struct dma_init_struct;

    s_gd30ad3344_dma_error = 0U;

    SPI_GD30AD3344_CS_LOW();
    
    /* 先发送高8位 */
    spi3_send_array[0] = (uint8_t)(half_word >> 8);
    spi3_send_array[1] = (uint8_t)half_word;
    
    /* 配置 DMA 发送通道 */
    dma_deinit(DMA1, DMA_CH4);
    dma_init_struct.periph_addr         = (uint32_t)&SPI_DATA(SPI_GD30AD3344);
    dma_init_struct.memory0_addr        = (uint32_t)spi3_send_array;
    dma_init_struct.direction           = DMA_MEMORY_TO_PERIPH;
    dma_init_struct.periph_memory_width = DMA_PERIPH_WIDTH_8BIT;
    dma_init_struct.priority            = DMA_PRIORITY_HIGH;
    dma_init_struct.number              = 2; /* 发送2个字节 */
    dma_init_struct.periph_inc          = DMA_PERIPH_INCREASE_DISABLE;
    dma_init_struct.memory_inc          = DMA_MEMORY_INCREASE_ENABLE;
    dma_init_struct.circular_mode       = DMA_CIRCULAR_MODE_DISABLE;
    dma_single_data_mode_init(DMA1, DMA_CH4, &dma_init_struct);
    dma_channel_subperipheral_select(DMA1, DMA_CH4, DMA_SUBPERI5);
    
    /* 配置 DMA 接收通道 */
    dma_deinit(DMA1, DMA_CH3);
    dma_init_struct.periph_addr         = (uint32_t)&SPI_DATA(SPI_GD30AD3344);
    dma_init_struct.memory0_addr        = (uint32_t)spi3_receive_array;
    dma_init_struct.direction           = DMA_PERIPH_TO_MEMORY;
    dma_init_struct.priority            = DMA_PRIORITY_HIGH;
    dma_single_data_mode_init(DMA1, DMA_CH3, &dma_init_struct);
    dma_channel_subperipheral_select(DMA1, DMA_CH3, DMA_SUBPERI5);
    
    /* 启用接收和发送的 DMA 通道 */
    dma_channel_enable(DMA1, DMA_CH3);
    dma_channel_enable(DMA1, DMA_CH4);
    
    /* 启用 SPI 的 DMA 接收和发送功能 */
    spi_dma_enable(SPI_GD30AD3344, SPI_DMA_RECEIVE);
    spi_dma_enable(SPI_GD30AD3344, SPI_DMA_TRANSMIT);
    
    if(0 != prv_gd30ad3344_wait_dma_done()) {
        spi_dma_disable(SPI_GD30AD3344, SPI_DMA_RECEIVE);
        spi_dma_disable(SPI_GD30AD3344, SPI_DMA_TRANSMIT);
        dma_channel_disable(DMA1, DMA_CH3);
        dma_channel_disable(DMA1, DMA_CH4);
        SPI_GD30AD3344_CS_HIGH();
        s_gd30ad3344_dma_error = 1U;
        return 0xFFFFU;
    }
    
    /* 禁用 DMA */
    spi_dma_disable(SPI_GD30AD3344, SPI_DMA_RECEIVE);
    spi_dma_disable(SPI_GD30AD3344, SPI_DMA_TRANSMIT);
    dma_channel_disable(DMA1, DMA_CH3);
    dma_channel_disable(DMA1, DMA_CH4);
    
    /* 组合接收到的数据 */
    rx_data = (uint16_t)(spi3_receive_array[0] << 8);
    rx_data |= spi3_receive_array[1];
    SPI_GD30AD3344_CS_HIGH();
    s_gd30ad3344_dma_error = 0U;
    return rx_data;
}

/**
 * @brief 使用 DMA 发送和接收多个字节
 * @param tx_buffer 发送缓冲区
 * @param rx_buffer 接收缓冲区
 * @param size 传输大小
 */
void spi_gd30ad3344_transmit_receive_dma(uint8_t *tx_buffer, uint8_t *rx_buffer, uint16_t size)
{
    uint16_t i;
    dma_single_data_parameter_struct dma_init_struct;

    s_gd30ad3344_dma_error = 0U;

    /* 检查传输大小是否超过缓冲区 */
    if (size > GD30AD3344_DMA_BUFFER_SIZE) {
        size = GD30AD3344_DMA_BUFFER_SIZE;
    }
    
    /* 准备发送数据 */
    for (i = 0U; i < size; i++) {
        spi3_send_array[i] = tx_buffer[i];
    }
    
    /* 配置 DMA 发送通道 */
    dma_deinit(DMA1, DMA_CH4);
    dma_init_struct.periph_addr         = (uint32_t)&SPI_DATA(SPI_GD30AD3344);
    dma_init_struct.memory0_addr        = (uint32_t)spi3_send_array;
    dma_init_struct.direction           = DMA_MEMORY_TO_PERIPH;
    dma_init_struct.periph_memory_width = DMA_PERIPH_WIDTH_8BIT;
    dma_init_struct.priority            = DMA_PRIORITY_HIGH;
    dma_init_struct.number              = size;
    dma_init_struct.periph_inc          = DMA_PERIPH_INCREASE_DISABLE;
    dma_init_struct.memory_inc          = DMA_MEMORY_INCREASE_ENABLE;
    dma_init_struct.circular_mode       = DMA_CIRCULAR_MODE_DISABLE;
    dma_single_data_mode_init(DMA1, DMA_CH4, &dma_init_struct);
    dma_channel_subperipheral_select(DMA1, DMA_CH4, DMA_SUBPERI5);
    
    /* 配置 DMA 接收通道 */
    dma_deinit(DMA1, DMA_CH3);
    dma_init_struct.periph_addr         = (uint32_t)&SPI_DATA(SPI_GD30AD3344);
    dma_init_struct.memory0_addr        = (uint32_t)spi3_receive_array;
    dma_init_struct.direction           = DMA_PERIPH_TO_MEMORY;
    dma_init_struct.priority            = DMA_PRIORITY_HIGH;
    dma_single_data_mode_init(DMA1, DMA_CH3, &dma_init_struct);
    dma_channel_subperipheral_select(DMA1, DMA_CH3, DMA_SUBPERI5);
    
    /* 启用接收和发送的 DMA 通道 */
    dma_channel_enable(DMA1, DMA_CH3);
    dma_channel_enable(DMA1, DMA_CH4);
    
    /* 启用 SPI 的 DMA 接收和发送功能 */
    spi_dma_enable(SPI_GD30AD3344, SPI_DMA_RECEIVE);
    spi_dma_enable(SPI_GD30AD3344, SPI_DMA_TRANSMIT);
    
    if(0 != prv_gd30ad3344_wait_dma_done()) {
        spi_dma_disable(SPI_GD30AD3344, SPI_DMA_RECEIVE);
        spi_dma_disable(SPI_GD30AD3344, SPI_DMA_TRANSMIT);
        dma_channel_disable(DMA1, DMA_CH3);
        dma_channel_disable(DMA1, DMA_CH4);
        s_gd30ad3344_dma_error = 1U;
        return;
    }
    
    /* 禁用 DMA */
    spi_dma_disable(SPI_GD30AD3344, SPI_DMA_RECEIVE);
    spi_dma_disable(SPI_GD30AD3344, SPI_DMA_TRANSMIT);
    dma_channel_disable(DMA1, DMA_CH3);
    dma_channel_disable(DMA1, DMA_CH4);
    
    /* 复制接收到的数据到接收缓冲区 */
    for (i = 0U; i < size; i++) {
        rx_buffer[i] = spi3_receive_array[i];
    }
    s_gd30ad3344_dma_error = 0U;
}

/**
 * @brief 等待 DMA 传输完成
 */
void spi_gd30ad3344_wait_for_dma_end(void)
{
    (void)prv_gd30ad3344_wait_dma_done();
}


GD30AD3344 GD30AD3344_InitStruct;

void GD30AD3344_Init(void)
{
    GD30AD3344_InitStruct.SS         = 0;        //写状态:0无作用 1开始单次转换（默认） 读的时候总是返回0 
    GD30AD3344_InitStruct.MUX        = 4;        // 0(默认)      1         2         3         4         5         6         7
                                                //AIN0~AIN1 AIN0~AIN3 AIN1~AIN3 AIN2~AIN3 AIN0~GND  AIN1~GND  AIN2~GND  AIN3~GND 
    GD30AD3344_InitStruct.PGA        = 1;       //    0         1       2(默认)     3         4         5         6         7
                                                // ±6.144V   ±4.096V   ±2.048V   ±1.024V   ±0.512V   ±0.256V   ±0.256V  ±0.256V
    GD30AD3344_InitStruct.MODE       = 0;        //0:连续转换模式    1:掉电，单次转换模式（默认） 
    GD30AD3344_InitStruct.DR         = 1;        //    0         1         2         3         4         5         6         7
                                                //  6.25SPS     12.5SPS   25SPS     50SPS     100SPS    250SPS    500SPS    1000SPS
    GD30AD3344_InitStruct.RESERVED_1 = 0;        //保留:写的时候写1，读的时候返回0或1 
    GD30AD3344_InitStruct.PULL_UP_EN = 0;        //0:关闭DOUT引脚上拉电阻(默认)    1:开启DOUT引脚上拉电阻
    GD30AD3344_InitStruct.NOP        = 1;        //0:不更新配置寄存器的数据  1:更新配置寄存器的数据(默认)  2:无效数据，且不更新配置寄存器数据
    GD30AD3344_InitStruct.RESERVED   = 1;        //保留:写的时候写1，读的时候返回0或1 

    (void)prv_gd30ad3344_apply_config(&GD30AD3344_InitStruct);
    my_printf(DEBUG_USART, "0x%4X", GD30AD3344_InitStruct_Value);
}

/*
 * 函数作用：
 *   将 GD30AD3344 切换到掉电/单次转换模式，减少 MCU 深睡期间 ADC 芯片自身待机电流。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   0：表示低功耗配置已成功下发。
 *  -1：表示 SPI DMA 等待超时或配置下发失败。
 * 说明：
 *   当前硬件没有给 GD30AD3344 做独立电源开关，因此这里利用器件本身 MODE=1
 *   的掉电/单次转换模式作为芯片级待机手段。
 */
int GD30AD3344_Enter_LowPower(void)
{
    GD30AD3344 sleep_config = GD30AD3344_InitStruct;

    sleep_config.SS = 0U;
    sleep_config.MODE = 1U;
    sleep_config.NOP = 1U;

    return prv_gd30ad3344_apply_config(&sleep_config);
}

/*
 * 函数作用：
 *   让 GD30AD3344 退出低功耗配置并恢复到工程默认运行态。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   0：表示恢复配置流程已经执行。
 *  -1：表示恢复配置下发失败。
 */
int GD30AD3344_Exit_LowPower(void)
{
    GD30AD3344_InitStruct.SS         = 0U;
    GD30AD3344_InitStruct.MUX        = 4U;
    GD30AD3344_InitStruct.PGA        = 1U;
    GD30AD3344_InitStruct.MODE       = 0U;
    GD30AD3344_InitStruct.DR         = 1U;
    GD30AD3344_InitStruct.RESERVED_1 = 0U;
    GD30AD3344_InitStruct.PULL_UP_EN = 0U;
    GD30AD3344_InitStruct.NOP        = 1U;
    GD30AD3344_InitStruct.RESERVED   = 1U;

    return prv_gd30ad3344_apply_config(&GD30AD3344_InitStruct);
}

static float PGA_DATA = 0.0f;
float ADS118_PGA_SET(GD30AD3344_PGA_TypeDef PGA)
{
    switch(PGA) {
    case GD30AD3344_PGA_6V144:
        PGA_DATA = 6.144f;
        break;
    case GD30AD3344_PGA_4V096:
        PGA_DATA = 4.096f;
        break;
    case GD30AD3344_PGA_2V048:
        PGA_DATA = 2.048f;
        break;
    case GD30AD3344_PGA_1V024:
        PGA_DATA = 1.024f;
        break;
    case GD30AD3344_PGA_0V512:
        PGA_DATA = 0.512f;
        break;
    case GD30AD3344_PGA_0V256:
        PGA_DATA = 0.256f;
        break;
    case GD30AD3344_PGA_0V064:
        PGA_DATA = 0.064f;
        break;
    default:
        /* 未知量程按最保守的小量程处理，避免返回沿用上一轮 PGA_DATA 的陈旧值。 */
        PGA_DATA = 0.064f;
        break;
    }

    return PGA_DATA;

}

/*
 * 函数作用：
 *   查询最近一次 GD30AD3344 底层 SPI/DMA 操作是否失败。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   0：表示最近一次操作没有记录 DMA 超时。
 *   非 0：表示最近一次操作发生 DMA 超时或参数错误。
 */
uint8_t GD30AD3344_GetLastError(void)
{
    return s_gd30ad3344_dma_error;
}

/*
 * 函数作用：
 *   读取指定 GD30AD3344 通道并换算为电压，同时把 SPI/DMA 错误显式返回给调用者。
 * 参数说明：
 *   CH：待采样的输入通道选择。
 *   Ref：待使用的 PGA 量程配置。
 *   out_voltage_v：输出电压值，单位 V；必须为有效指针。
 * 返回值说明：
 *   0：表示采样成功，out_voltage_v 已更新。
 *  -1：表示参数无效或 SPI DMA 传输失败，调用者不能使用本次电压值。
 */
int GD30AD3344_AD_Read(GD30AD3344_Channel_TypeDef CH, GD30AD3344_PGA_TypeDef Ref, float *out_voltage_v)
{
    uint16_t raw_data;
    float result = 0.0;

    if(NULL == out_voltage_v) {
        s_gd30ad3344_dma_error = 1U;
        return -1;
    }

    GD30AD3344_InitStruct.MUX = CH;
    GD30AD3344_InitStruct.PGA = Ref;

    raw_data = spi_gd30ad3344_send_halfword_dma(GD30AD3344_InitStruct_Value);
    if(0U != s_gd30ad3344_dma_error) {
        return -1;
    }
    
    result = (float)raw_data * ADS118_PGA_SET(Ref) / 32768;
    *out_voltage_v = result;
    return 0;
}
