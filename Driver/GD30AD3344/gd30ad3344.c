/*!
    \file    gd30ad3344.c
    \brief   gd30ad3344 driver

    \version 2024-10-08, V1.0.0, firmware for GD30AD3344
*/

#include "gd30ad3344.h"

// SPI DMA等待超时，避免硬件异常时卡死。
#define GD30AD3344_SPI_WAIT_TIMEOUT     0x00FFFFFFUL
#define GD30AD3344_DMA_BUFFER_SIZE      12U

/* GD30AD3344 所在 SPI3 引脚资源定义。 */
#define GD30AD3344_SPI_GPIO_PORT       GPIOE
#define GD30AD3344_SPI_GPIO_CLOCK      RCU_GPIOE
#define GD30AD3344_SPI_SCK_PIN         GPIO_PIN_12
#define GD30AD3344_SPI_MISO_PIN        GPIO_PIN_13
#define GD30AD3344_SPI_MOSI_PIN        GPIO_PIN_14
#define GD30AD3344_SPI_CS_PIN          GPIO_PIN_10
#define GD30AD3344_SPI_AF              GPIO_AF_5

#define SPI_GD30AD3344                 SPI3
#define SPI_GD30AD3344_CS_LOW()        gpio_bit_reset(GD30AD3344_SPI_GPIO_PORT, GD30AD3344_SPI_CS_PIN)
#define SPI_GD30AD3344_CS_HIGH()       gpio_bit_set(GD30AD3344_SPI_GPIO_PORT, GD30AD3344_SPI_CS_PIN)

/* DMA发送缓冲。 */
static uint8_t spi3_send_array[GD30AD3344_DMA_BUFFER_SIZE];

/* DMA接收缓冲。 */
static uint8_t spi3_receive_array[GD30AD3344_DMA_BUFFER_SIZE];

// 最近一次SPI/DMA错误标志。
static uint8_t s_gd30ad3344_dma_error;

static GD30AD3344 s_adc_cfg;

/* 初始化GD30AD3344使用的SPI3、GPIO和DMA时钟资源。 */
static void gd30ad3344_bus_init(void)
{
    spi_parameter_struct spi_init_struct;

    rcu_periph_clock_enable(GD30AD3344_SPI_GPIO_CLOCK);
    rcu_periph_clock_enable(RCU_SPI3);
    rcu_periph_clock_enable(RCU_DMA1);

    gpio_af_set(GD30AD3344_SPI_GPIO_PORT, GD30AD3344_SPI_AF, GD30AD3344_SPI_SCK_PIN | GD30AD3344_SPI_MISO_PIN | GD30AD3344_SPI_MOSI_PIN);
    gpio_mode_set(GD30AD3344_SPI_GPIO_PORT, GPIO_MODE_AF, GPIO_PUPD_NONE, GD30AD3344_SPI_SCK_PIN | GD30AD3344_SPI_MISO_PIN | GD30AD3344_SPI_MOSI_PIN);
    gpio_output_options_set(GD30AD3344_SPI_GPIO_PORT, GPIO_OTYPE_PP, GPIO_OSPEED_50MHZ, GD30AD3344_SPI_SCK_PIN | GD30AD3344_SPI_MISO_PIN | GD30AD3344_SPI_MOSI_PIN);

    gpio_mode_set(GD30AD3344_SPI_GPIO_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, GD30AD3344_SPI_CS_PIN);
    gpio_output_options_set(GD30AD3344_SPI_GPIO_PORT, GPIO_OTYPE_PP, GPIO_OSPEED_50MHZ, GD30AD3344_SPI_CS_PIN);
    SPI_GD30AD3344_CS_HIGH();

    spi_struct_para_init(&spi_init_struct);
    spi_init_struct.trans_mode = SPI_TRANSMODE_FULLDUPLEX;
    spi_init_struct.device_mode = SPI_MASTER;
    spi_init_struct.frame_size = SPI_FRAMESIZE_8BIT;
    spi_init_struct.clock_polarity_phase = SPI_CK_PL_LOW_PH_2EDGE;
    spi_init_struct.nss = SPI_NSS_SOFT;
    spi_init_struct.prescale = SPI_PSC_8;
    spi_init_struct.endian = SPI_ENDIAN_MSB;
    spi_init(SPI_GD30AD3344, &spi_init_struct);
}

// 打包当前GD30AD3344配置寄存器。
static uint16_t gd30ad3344_cfg_value(const GD30AD3344 *cfg)
{
    return (uint16_t)((cfg->SS << 15) | (cfg->MUX << 12) | (cfg->PGA << 9) | (cfg->MODE << 8) | (cfg->DR << 5) | (cfg->RESERVED_1 << 4) | (cfg->PULL_UP_EN << 3) | (cfg->NOP << 1) | (cfg->RESERVED << 0));
}

// 等DMA完成，0成功，-1超时。
static int prv_gd30ad3344_wait_dma_done(void)
{
    uint32_t timeout = GD30AD3344_SPI_WAIT_TIMEOUT;

    while(RESET == dma_flag_get(DMA1, DMA_CH3, DMA_FLAG_FTF))
	{
        if(0 == timeout)
		{
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

static uint16_t spi_gd30ad3344_send_halfword_dma(uint16_t half_word)
{
    uint16_t rx_data;
    dma_single_data_parameter_struct dma_init_struct;

    s_gd30ad3344_dma_error = 0;

    SPI_GD30AD3344_CS_LOW();

    spi3_send_array[0] = (uint8_t)(half_word >> 8);
    spi3_send_array[1] = (uint8_t)half_word;

    dma_deinit(DMA1, DMA_CH4);
    dma_single_data_para_struct_init(&dma_init_struct);
    dma_init_struct.periph_addr         = (uint32_t)&SPI_DATA(SPI_GD30AD3344);
    dma_init_struct.memory0_addr        = (uint32_t)spi3_send_array;
    dma_init_struct.direction           = DMA_MEMORY_TO_PERIPH;
    dma_init_struct.periph_memory_width = DMA_PERIPH_WIDTH_8BIT;
    dma_init_struct.priority            = DMA_PRIORITY_HIGH;
    dma_init_struct.number              = 2;
    dma_init_struct.periph_inc          = DMA_PERIPH_INCREASE_DISABLE;
    dma_init_struct.memory_inc          = DMA_MEMORY_INCREASE_ENABLE;
    dma_init_struct.circular_mode       = DMA_CIRCULAR_MODE_DISABLE;
    dma_single_data_mode_init(DMA1, DMA_CH4, &dma_init_struct);
    dma_channel_subperipheral_select(DMA1, DMA_CH4, DMA_SUBPERI5);

    dma_deinit(DMA1, DMA_CH3);
    dma_init_struct.periph_addr         = (uint32_t)&SPI_DATA(SPI_GD30AD3344);
    dma_init_struct.memory0_addr        = (uint32_t)spi3_receive_array;
    dma_init_struct.direction           = DMA_PERIPH_TO_MEMORY;
    dma_init_struct.priority            = DMA_PRIORITY_HIGH;
    dma_single_data_mode_init(DMA1, DMA_CH3, &dma_init_struct);
    dma_channel_subperipheral_select(DMA1, DMA_CH3, DMA_SUBPERI5);

    dma_channel_enable(DMA1, DMA_CH3);
    dma_channel_enable(DMA1, DMA_CH4);

    spi_dma_enable(SPI_GD30AD3344, SPI_DMA_RECEIVE);
    spi_dma_enable(SPI_GD30AD3344, SPI_DMA_TRANSMIT);

    if(0 != prv_gd30ad3344_wait_dma_done())
	{
        spi_dma_disable(SPI_GD30AD3344, SPI_DMA_RECEIVE);
        spi_dma_disable(SPI_GD30AD3344, SPI_DMA_TRANSMIT);
        dma_channel_disable(DMA1, DMA_CH3);
        dma_channel_disable(DMA1, DMA_CH4);
        SPI_GD30AD3344_CS_HIGH();
        s_gd30ad3344_dma_error = 1;
        return 0xFFFFU;
    }

    spi_dma_disable(SPI_GD30AD3344, SPI_DMA_RECEIVE);
    spi_dma_disable(SPI_GD30AD3344, SPI_DMA_TRANSMIT);
    dma_channel_disable(DMA1, DMA_CH3);
    dma_channel_disable(DMA1, DMA_CH4);

    rx_data = (uint16_t)(spi3_receive_array[0] << 8);
    rx_data |= spi3_receive_array[1];
    SPI_GD30AD3344_CS_HIGH();
    s_gd30ad3344_dma_error = 0;
    return rx_data;
}


// 写GD30AD3344配置寄存器。
static int prv_gd30ad3344_apply_config(const GD30AD3344 *config)
{
    uint16_t config_value;
    uint16_t rx_value;

    if(!config)
	{
        return -1;
    }

    config_value = gd30ad3344_cfg_value(config);

    spi_enable(SPI_GD30AD3344);
    rx_value = spi_gd30ad3344_send_halfword_dma(config_value);
    (void)rx_value;
    if(0 != s_gd30ad3344_dma_error)
	{
        return -1;
    }

    return 0;
}


void GD30AD3344_Init(void)
{
    gd30ad3344_bus_init();

    s_adc_cfg.SS         = 0;        //写状态:0无作用 1开始单次转换（默认） 读的时候总是返回0
    s_adc_cfg.MUX        = 4;        // 0(默认)      1         2         3         4         5         6         7
                                                //AIN0~AIN1 AIN0~AIN3 AIN1~AIN3 AIN2~AIN3 AIN0~GND  AIN1~GND  AIN2~GND  AIN3~GND
    s_adc_cfg.PGA        = 1;       //    0         1       2(默认)     3         4         5         6         7
                                                // ±6.144V   ±4.096V   ±2.048V   ±1.024V   ±0.512V   ±0.256V   ±0.256V  ±0.256V
    s_adc_cfg.MODE       = 0;        //0:连续转换模式    1:掉电，单次转换模式（默认）
    s_adc_cfg.DR         = 1;        //    0         1         2         3         4         5         6         7
                                                //  6.25SPS     12.5SPS   25SPS     50SPS     100SPS    250SPS    500SPS    1000SPS
    s_adc_cfg.RESERVED_1 = 0;        //保留:写的时候写1，读的时候返回0或1
    s_adc_cfg.PULL_UP_EN = 0;        //0:关闭DOUT引脚上拉电阻(默认)    1:开启DOUT引脚上拉电阻
    s_adc_cfg.NOP        = 1;        //0:不更新配置寄存器的数据  1:更新配置寄存器的数据(默认)  2:无效数据，且不更新配置寄存器数据
    s_adc_cfg.RESERVED   = 1;        //保留:写的时候写1，读的时候返回0或1

    (void)prv_gd30ad3344_apply_config(&s_adc_cfg);
}

static float gd30ad3344_pga_v(GD30AD3344_PGA_TypeDef PGA)
{
    switch(PGA) {
    case GD30AD3344_PGA_6V144:
        return 6.144f;
    case GD30AD3344_PGA_4V096:
        return 4.096f;
    case GD30AD3344_PGA_2V048:
        return 2.048f;
    case GD30AD3344_PGA_1V024:
        return 1.024f;
    case GD30AD3344_PGA_0V512:
        return 0.512f;
    case GD30AD3344_PGA_0V256:
        return 0.256f;
    case GD30AD3344_PGA_0V064:
        return 0.064f;
    default:
        /* 未知量程按最小量程处理。 */
        return 0.064f;
    }
}

// 读取通道电压，0成功，-1失败。
int GD30AD3344_AD_Read(GD30AD3344_Channel_TypeDef CH, GD30AD3344_PGA_TypeDef Ref, float *out_voltage_v)
{
    uint16_t raw_data;
    float result = 0.0;

    if(!out_voltage_v)
	{
        s_gd30ad3344_dma_error = 1;
        return -1;
    }

    s_adc_cfg.MUX = CH;
    s_adc_cfg.PGA = Ref;

    raw_data = spi_gd30ad3344_send_halfword_dma(gd30ad3344_cfg_value(&s_adc_cfg));
    if(0 != s_gd30ad3344_dma_error)
	{
        return -1;
    }

    result = (float)raw_data * gd30ad3344_pga_v(Ref) / 32768;
    *out_voltage_v = result;
    return 0;
}
