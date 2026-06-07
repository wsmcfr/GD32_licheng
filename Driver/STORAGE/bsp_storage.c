#include "bsp_storage.h"

/* 初始化 GD30AD3344 底层总线资源：打开时钟、配置 SPI3 GPIO 及片选、初始化 SPI3 参数 */
void bsp_gd30ad3344_init(void)
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

    spi_init_struct.trans_mode = SPI_TRANSMODE_FULLDUPLEX;
    spi_init_struct.device_mode = SPI_MASTER;
    spi_init_struct.frame_size = SPI_FRAMESIZE_8BIT;
    spi_init_struct.clock_polarity_phase = SPI_CK_PL_LOW_PH_2EDGE;
    spi_init_struct.nss = SPI_NSS_SOFT;
    spi_init_struct.prescale = SPI_PSC_8;
    spi_init_struct.endian = SPI_ENDIAN_MSB;
    spi_init(SPI_GD30AD3344, &spi_init_struct);

    GD30AD3344_Init();
}
