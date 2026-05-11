#include "bsp_storage.h"

/*
 * 函数作用：
 *   初始化 SPI Flash 底层总线资源。
 * 主要流程：
 *   1. 打开 GPIO、SPI0、DMA 时钟。
 *   2. 配置 SPI0 的 SCK/MISO/MOSI 复用引脚。
 *   3. 配置片选引脚为普通推挽输出。
 *   4. 初始化 SPI0 参数并调用 Flash 组件初始化。
 */
void bsp_gd25qxx_init(void)
{
    spi_parameter_struct spi_init_struct;

    rcu_periph_clock_enable(GD25QXX_SPI_GPIO_CLOCK);
    rcu_periph_clock_enable(GD25QXX_SPI_CS_GPIO_CLOCK);
    rcu_periph_clock_enable(RCU_SPI0);
    rcu_periph_clock_enable(RCU_DMA0);

    gpio_af_set(GD25QXX_SPI_GPIO_PORT,
                GD25QXX_SPI_AF,
                GD25QXX_SPI_SCK_PIN | GD25QXX_SPI_MISO_PIN | GD25QXX_SPI_MOSI_PIN);
    gpio_mode_set(GD25QXX_SPI_GPIO_PORT,
                  GPIO_MODE_AF,
                  GPIO_PUPD_NONE,
                  GD25QXX_SPI_SCK_PIN | GD25QXX_SPI_MISO_PIN | GD25QXX_SPI_MOSI_PIN);
    gpio_output_options_set(GD25QXX_SPI_GPIO_PORT,
                            GPIO_OTYPE_PP,
                            GPIO_OSPEED_50MHZ,
                            GD25QXX_SPI_SCK_PIN | GD25QXX_SPI_MISO_PIN | GD25QXX_SPI_MOSI_PIN);

    gpio_mode_set(GD25QXX_SPI_CS_GPIO_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, GD25QXX_SPI_CS_PIN);
    gpio_output_options_set(GD25QXX_SPI_CS_GPIO_PORT,
                            GPIO_OTYPE_PP,
                            GPIO_OSPEED_50MHZ,
                            GD25QXX_SPI_CS_PIN);

    spi_init_struct.trans_mode = SPI_TRANSMODE_FULLDUPLEX;
    spi_init_struct.device_mode = SPI_MASTER;
    spi_init_struct.frame_size = SPI_FRAMESIZE_8BIT;
    spi_init_struct.clock_polarity_phase = SPI_CK_PL_HIGH_PH_2EDGE;
    spi_init_struct.nss = SPI_NSS_SOFT;
    spi_init_struct.prescale = SPI_PSC_8;
    spi_init_struct.endian = SPI_ENDIAN_MSB;
    spi_init(SPI_FLASH, &spi_init_struct);

    spi_flash_init();
}

/*
 * 函数作用：
 *   初始化 GD30AD3344 底层总线资源。
 * 主要流程：
 *   1. 打开 GPIOE、SPI3、DMA1 时钟。
 *   2. 配置 SPI3 三线复用功能和片选 GPIO。
 *   3. 初始化 SPI3 参数并调用 ADC 组件初始化。
 */
void bsp_gd30ad3344_init(void)
{
    spi_parameter_struct spi_init_struct;

    rcu_periph_clock_enable(GD30AD3344_SPI_GPIO_CLOCK);
    rcu_periph_clock_enable(RCU_SPI3);
    rcu_periph_clock_enable(RCU_DMA1);

    gpio_af_set(GD30AD3344_SPI_GPIO_PORT,
                GD30AD3344_SPI_AF,
                GD30AD3344_SPI_SCK_PIN | GD30AD3344_SPI_MISO_PIN | GD30AD3344_SPI_MOSI_PIN);
    gpio_mode_set(GD30AD3344_SPI_GPIO_PORT,
                  GPIO_MODE_AF,
                  GPIO_PUPD_NONE,
                  GD30AD3344_SPI_SCK_PIN | GD30AD3344_SPI_MISO_PIN | GD30AD3344_SPI_MOSI_PIN);
    gpio_output_options_set(GD30AD3344_SPI_GPIO_PORT,
                            GPIO_OTYPE_PP,
                            GPIO_OSPEED_50MHZ,
                            GD30AD3344_SPI_SCK_PIN | GD30AD3344_SPI_MISO_PIN | GD30AD3344_SPI_MOSI_PIN);

    gpio_mode_set(GD30AD3344_SPI_GPIO_PORT,
                  GPIO_MODE_OUTPUT,
                  GPIO_PUPD_NONE,
                  GD30AD3344_SPI_CS_PIN);
    gpio_output_options_set(GD30AD3344_SPI_GPIO_PORT,
                            GPIO_OTYPE_PP,
                            GPIO_OSPEED_50MHZ,
                            GD30AD3344_SPI_CS_PIN);

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
