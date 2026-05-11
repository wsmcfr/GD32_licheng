/* Licence
* Company: MCUSTUDIO
* Auther: Ahypnis.
* Version: V0.10
* Time: 2025/06/05
* Note:
*/
#include "mcu_cmic_gd32f470vet6.h"

/* OLED 命令和数据缓冲区 */
__IO uint8_t oled_cmd_buf[2] = {0x00, 0x00};  // 命令缓冲区：控制字节 + 命令
__IO uint8_t oled_data_buf[2] = {0x40, 0x00}; // 数据缓冲区：控制字节 + 数据

/* SPI3 DMA 相关缓冲区 */
uint8_t spi3_send_array[ARRAYSIZE] = {0};    // SPI3 DMA 发送缓冲区
uint8_t spi3_receive_array[ARRAYSIZE] = {0}; // SPI3 DMA 接收缓冲区

/* SPI0 DMA 相关缓冲区 */
uint8_t spi1_send_array[ARRAYSIZE] = {0};    // SPI0 DMA 发送缓冲区
uint8_t spi1_receive_array[ARRAYSIZE] = {0}; // SPI0 DMA 接收缓冲区

/* 串口 DMA 接收缓冲区 */
uint8_t usart0_rxbuffer[512];
uint8_t usart1_rxbuffer[256];
uint8_t usart2_rxbuffer[256];
uint8_t usart5_rxbuffer[256];

/* ADC 采样值缓冲区 */
uint16_t adc_value[2];

/* DAC 输出缓冲区 */
uint16_t convertarr[CONVERT_NUM] = {0};

/* RTC */
rtc_parameter_struct rtc_initpara;
rtc_alarm_struct  rtc_alarm;
__IO uint32_t prescaler_a = 0, prescaler_s = 0;
uint32_t RTCSRC_FLAG = 0;

void bsp_led_init(void)
{
    /* enable the led clock */
    rcu_periph_clock_enable(LED_CLK_PORT);
    /* configure led GPIO port */ 
    gpio_mode_set(LED_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_PULLUP, LED1_PIN | LED2_PIN | LED3_PIN | LED4_PIN | LED5_PIN | LED6_PIN);
    gpio_output_options_set(LED_PORT, GPIO_OTYPE_PP, GPIO_OSPEED_50MHZ, LED1_PIN | LED2_PIN | LED3_PIN | LED4_PIN | LED5_PIN | LED6_PIN);

    LED1_OFF;
    LED2_OFF;
    LED3_OFF;
    LED4_OFF;
    LED5_OFF;
    LED6_OFF;
}

void bsp_btn_init(void)
{
    /* enable the led clock */
    rcu_periph_clock_enable(KEYB_CLK_PORT);
    rcu_periph_clock_enable(KEYE_CLK_PORT);
    rcu_periph_clock_enable(KEYA_CLK_PORT);
    
    /* configure led GPIO port */ 
    gpio_mode_set(KEYE_PORT, GPIO_MODE_INPUT, GPIO_PUPD_PULLUP, KEY1_PIN | KEY2_PIN | KEY3_PIN | KEY4_PIN | KEY5_PIN);
    gpio_mode_set(KEYB_PORT, GPIO_MODE_INPUT, GPIO_PUPD_PULLUP, KEY6_PIN);
    gpio_mode_set(KEYA_PORT, GPIO_MODE_INPUT, GPIO_PUPD_PULLUP, KEYW_PIN);
}

void bsp_wkup_key_exti_init(void)
{
    rcu_periph_clock_enable(KEYA_CLK_PORT);
    rcu_periph_clock_enable(RCU_SYSCFG);

    gpio_mode_set(KEYA_PORT, GPIO_MODE_INPUT, GPIO_PUPD_PULLUP, KEYW_PIN);

    syscfg_exti_line_config(EXTI_SOURCE_GPIOA, EXTI_SOURCE_PIN0);
    exti_init(EXTI_0, EXTI_INTERRUPT, EXTI_TRIG_BOTH);
    exti_interrupt_flag_clear(EXTI_0);
    nvic_irq_enable(EXTI0_IRQn, 1U, 0U);
}

static void bsp_usart_disable_for_deepsleep(void)
{
    usart_interrupt_disable(USART0, USART_INT_IDLE);
    nvic_irq_disable(USART0_IRQn);
    usart_dma_receive_config(USART0, USART_RECEIVE_DMA_DISABLE);
    dma_channel_disable(USART0_RX_DMA_PERIPH, USART0_RX_DMA_CHANNEL);
    usart_disable(USART0);

    usart_interrupt_disable(USART1, USART_INT_IDLE);
    nvic_irq_disable(USART1_IRQn);
    usart_dma_receive_config(USART1, USART_RECEIVE_DMA_DISABLE);
    dma_channel_disable(USART1_RX_DMA_PERIPH, USART1_RX_DMA_CHANNEL);
    usart_disable(USART1);

    usart_interrupt_disable(USART2, USART_INT_IDLE);
    nvic_irq_disable(USART2_IRQn);
    usart_dma_receive_config(USART2, USART_RECEIVE_DMA_DISABLE);
    dma_channel_disable(USART2_RX_DMA_PERIPH, USART2_RX_DMA_CHANNEL);
    usart_disable(USART2);

    usart_interrupt_disable(USART5, USART_INT_IDLE);
    nvic_irq_disable(USART5_IRQn);
    usart_dma_receive_config(USART5, USART_RECEIVE_DMA_DISABLE);
    dma_channel_disable(USART5_RX_DMA_PERIPH, USART5_RX_DMA_CHANNEL);
    usart_disable(USART5);
}

static void bsp_oled_disable_for_deepsleep(void)
{
    OLED_Display_Off();

    i2c_dma_config(I2C0, I2C_DMA_OFF);
    dma_channel_disable(DMA0, DMA_CH6);
    i2c_disable(I2C0);

    gpio_mode_set(OLED_PORT, GPIO_MODE_ANALOG, GPIO_PUPD_NONE, OLED_DAT_PIN | OLED_CLK_PIN);
}

static void bsp_spi_disable_for_deepsleep(void)
{
    SPI_FLASH_CS_HIGH();
    SPI_GD30AD3344_CS_HIGH();

    spi_dma_disable(SPI0, SPI_DMA_RECEIVE);
    spi_dma_disable(SPI0, SPI_DMA_TRANSMIT);
    spi_dma_disable(SPI3, SPI_DMA_RECEIVE);
    spi_dma_disable(SPI3, SPI_DMA_TRANSMIT);

    dma_channel_disable(DMA1, DMA_CH2);
    dma_channel_disable(DMA1, DMA_CH3);
    dma_channel_disable(DMA1, DMA_CH4);

    spi_disable(SPI0);
    spi_disable(SPI3);
}

static void bsp_sdio_disable_for_deepsleep(void)
{
    nvic_irq_disable(SDIO_IRQn);
    sdio_dma_disable();
    sdio_clock_disable();
    sd_power_off();
    sdio_deinit();
    dma_channel_disable(DMA1, DMA_CH3);
}

static void bsp_gpio_enter_deepsleep_state(void)
{
    rcu_periph_clock_enable(RCU_GPIOA);
    rcu_periph_clock_enable(RCU_GPIOB);
    rcu_periph_clock_enable(RCU_GPIOC);
    rcu_periph_clock_enable(RCU_GPIOD);
    rcu_periph_clock_enable(RCU_GPIOE);

    LED1_OFF;
    LED2_OFF;
    LED3_OFF;
    LED4_OFF;
    LED5_OFF;
    LED6_OFF;

    gpio_mode_set(KEYE_PORT, GPIO_MODE_ANALOG, GPIO_PUPD_NONE,
                  KEY1_PIN | KEY2_PIN | KEY3_PIN | KEY4_PIN | KEY5_PIN);
    gpio_mode_set(KEYB_PORT, GPIO_MODE_ANALOG, GPIO_PUPD_NONE, KEY6_PIN);

    gpio_mode_set(USART0_TX_PORT, GPIO_MODE_ANALOG, GPIO_PUPD_NONE, USART0_TX_PIN);
    gpio_mode_set(USART0_RX_PORT, GPIO_MODE_ANALOG, GPIO_PUPD_NONE, USART0_RX_PIN);
    gpio_mode_set(USART1_TX_PORT, GPIO_MODE_ANALOG, GPIO_PUPD_NONE, USART1_TX_PIN);
    gpio_mode_set(USART1_RX_PORT, GPIO_MODE_ANALOG, GPIO_PUPD_NONE, USART1_RX_PIN);
    gpio_mode_set(USART2_TX_PORT, GPIO_MODE_ANALOG, GPIO_PUPD_NONE, USART2_TX_PIN);
    gpio_mode_set(USART2_RX_PORT, GPIO_MODE_ANALOG, GPIO_PUPD_NONE, USART2_RX_PIN);
    gpio_mode_set(USART5_TX_PORT, GPIO_MODE_ANALOG, GPIO_PUPD_NONE, USART5_TX_PIN);
    gpio_mode_set(USART5_RX_PORT, GPIO_MODE_ANALOG, GPIO_PUPD_NONE, USART5_RX_PIN);

    gpio_mode_set(OLED_PORT, GPIO_MODE_ANALOG, GPIO_PUPD_NONE,
                  OLED_DAT_PIN | OLED_CLK_PIN);

    gpio_mode_set(SPI_PORT, GPIO_MODE_ANALOG, GPIO_PUPD_NONE,
                  SPI_SCK | SPI_MISO | SPI_MOSI);
    gpio_mode_set(SPI_CS_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, SPI_NSS);
    gpio_output_options_set(SPI_CS_PORT, GPIO_OTYPE_PP, GPIO_OSPEED_2MHZ, SPI_NSS);
    GPIO_BOP(SPI_CS_PORT) = SPI_NSS;

    gpio_mode_set(SPI3_PORT, GPIO_MODE_ANALOG, GPIO_PUPD_NONE,
                  SPI3_SCK | SPI3_MISO | SPI3_MOSI);
    gpio_mode_set(SPI3_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, SPI3_NSS);
    gpio_output_options_set(SPI3_PORT, GPIO_OTYPE_PP, GPIO_OSPEED_2MHZ, SPI3_NSS);
    GPIO_BOP(SPI3_PORT) = SPI3_NSS;

    gpio_mode_set(ADC1_PORT, GPIO_MODE_ANALOG, GPIO_PUPD_NONE, ADC1_PIN | ADC_VREF_PIN);
    gpio_mode_set(DAC1_PORT, GPIO_MODE_ANALOG, GPIO_PUPD_NONE, DAC1_PIN);

    gpio_mode_set(GPIOC, GPIO_MODE_ANALOG, GPIO_PUPD_NONE,
                  GPIO_PIN_8 | GPIO_PIN_9 | GPIO_PIN_10 | GPIO_PIN_11 | GPIO_PIN_12);
    gpio_mode_set(GPIOD, GPIO_MODE_ANALOG, GPIO_PUPD_NONE, GPIO_PIN_2);

    gpio_mode_set(KEYA_PORT, GPIO_MODE_INPUT, GPIO_PUPD_PULLUP, KEYW_PIN);
}

static void bsp_deepsleep_reinit_after_wakeup(void)
{
    SystemInit();
    SystemCoreClockUpdate();
    systick_config();
    update_perf_counter();
    bsp_led_init();
    bsp_btn_init();
    bsp_usart_init();
    bsp_oled_init();
    OLED_Init();
    bsp_adc_init();
    bsp_dac_init();
    bsp_gd25qxx_init();
    bsp_gd30ad3344_init();
    sd_fatfs_init();
}

void bsp_enter_deepsleep(void)
{
    rcu_periph_clock_enable(RCU_PMU);

    __disable_irq();

    bsp_usart_disable_for_deepsleep();
    bsp_oled_disable_for_deepsleep();
    bsp_spi_disable_for_deepsleep();
    bsp_sdio_disable_for_deepsleep();

    adc_disable(ADC0);
    adc_dma_mode_disable(ADC0);
    dma_channel_disable(DMA1, DMA_CH0);
    dac_disable(DAC0, DAC_OUT0);
    dac_dma_disable(DAC0, DAC_OUT0);
    dma_channel_disable(DMA0, DMA_CH5);
    timer_disable(TIMER5);

    bsp_gpio_enter_deepsleep_state();
    bsp_wkup_key_exti_init();

    pmu_flag_clear(PMU_FLAG_RESET_WAKEUP);
    pmu_flag_clear(PMU_FLAG_RESET_STANDBY);

    before_cycle_counter_reconfiguration();
    SysTick->CTRL &= ~SysTick_CTRL_TICKINT_Msk;

    __enable_irq();

    pmu_to_deepsleepmode(PMU_LDO_LOWPOWER, PMU_LOWDRIVER_ENABLE, WFI_CMD);

    bsp_deepsleep_reinit_after_wakeup();
}

/*!
    \brief      configure USART
    \param[in]  none
    \param[out] none
    \retval     none
*/
void bsp_usart0_init(void)
{
    dma_single_data_parameter_struct dma_init_struct;
    
    rcu_periph_clock_enable(RCU_DMA1);
    
    dma_deinit(USART0_RX_DMA_PERIPH, USART0_RX_DMA_CHANNEL);
    dma_init_struct.direction = DMA_PERIPH_TO_MEMORY;
    dma_init_struct.memory0_addr = (uint32_t)usart0_rxbuffer;
    dma_init_struct.memory_inc = DMA_MEMORY_INCREASE_ENABLE;
    dma_init_struct.number = sizeof(usart0_rxbuffer);
    dma_init_struct.periph_addr = USART0_RDATA_ADDRESS;
    dma_init_struct.periph_inc = DMA_PERIPH_INCREASE_DISABLE;
    dma_init_struct.periph_memory_width = DMA_PERIPH_WIDTH_8BIT;
    dma_init_struct.priority = DMA_PRIORITY_ULTRA_HIGH;
    dma_single_data_mode_init(USART0_RX_DMA_PERIPH, USART0_RX_DMA_CHANNEL, &dma_init_struct);
    
    /* configure DMA mode */
    dma_circulation_disable(USART0_RX_DMA_PERIPH, USART0_RX_DMA_CHANNEL);
    dma_channel_subperipheral_select(USART0_RX_DMA_PERIPH, USART0_RX_DMA_CHANNEL, USART0_RX_DMA_SUBPERI);
    /* enable DMA1 channel2 */
    dma_channel_enable(USART0_RX_DMA_PERIPH, USART0_RX_DMA_CHANNEL);
    
    /* enable GPIO clock */
    rcu_periph_clock_enable(USART0_CLK_PORT);

    /* enable USART clock */
    rcu_periph_clock_enable(RCU_USART0);
    
    /* connect port to USARTx_Tx */
    gpio_af_set(USART0_PORT, AF_USART0, USART0_TX);

    /* connect port to USARTx_Rx */
    gpio_af_set(USART0_PORT, AF_USART0, USART0_RX);

    /* configure USART Tx as alternate function push-pull */
    gpio_mode_set(USART0_PORT, GPIO_MODE_AF, GPIO_PUPD_PULLUP, USART0_TX);
    gpio_output_options_set(USART0_PORT, GPIO_OTYPE_PP, GPIO_OSPEED_50MHZ, USART0_TX);

    /* configure USART Rx as alternate function push-pull */
    gpio_mode_set(USART0_PORT, GPIO_MODE_AF, GPIO_PUPD_PULLUP, USART0_RX);
    gpio_output_options_set(USART0_PORT, GPIO_OTYPE_PP, GPIO_OSPEED_50MHZ, USART0_RX);

    /* configure USART */
    usart_deinit(USART0);
    usart_baudrate_set(USART0, 115200U);
    usart_receive_config(USART0, USART_RECEIVE_ENABLE);
    usart_transmit_config(USART0, USART_TRANSMIT_ENABLE);
    usart_dma_receive_config(USART0, USART_RECEIVE_DMA_ENABLE);
    usart_enable(USART0);
    
    nvic_irq_enable(USART0_IRQn, 0, 0);
    
    usart_interrupt_enable(USART0, USART_INT_IDLE);
}

void bsp_usart_init(void)
{
    bsp_usart0_init();
}

void bsp_usart1_init(void)
{
    dma_single_data_parameter_struct dma_init_struct;

    rcu_periph_clock_enable(RCU_DMA0);
    rcu_periph_clock_enable(RCU_GPIOD);
    rcu_periph_clock_enable(RCU_USART1);

    dma_deinit(USART1_RX_DMA_PERIPH, USART1_RX_DMA_CHANNEL);
    dma_init_struct.direction = DMA_PERIPH_TO_MEMORY;
    dma_init_struct.memory0_addr = (uint32_t)usart1_rxbuffer;
    dma_init_struct.memory_inc = DMA_MEMORY_INCREASE_ENABLE;
    dma_init_struct.number = sizeof(usart1_rxbuffer);
    dma_init_struct.periph_addr = USART1_RDATA_ADDRESS;
    dma_init_struct.periph_inc = DMA_PERIPH_INCREASE_DISABLE;
    dma_init_struct.periph_memory_width = DMA_PERIPH_WIDTH_8BIT;
    dma_init_struct.priority = DMA_PRIORITY_ULTRA_HIGH;
    dma_single_data_mode_init(USART1_RX_DMA_PERIPH, USART1_RX_DMA_CHANNEL, &dma_init_struct);

    dma_circulation_disable(USART1_RX_DMA_PERIPH, USART1_RX_DMA_CHANNEL);
    dma_channel_subperipheral_select(USART1_RX_DMA_PERIPH, USART1_RX_DMA_CHANNEL, USART1_RX_DMA_SUBPERI);
    dma_channel_enable(USART1_RX_DMA_PERIPH, USART1_RX_DMA_CHANNEL);

    gpio_af_set(USART1_TX_PORT, AF_USART1, USART1_TX_PIN | USART1_RX_PIN);
    gpio_mode_set(USART1_TX_PORT, GPIO_MODE_AF, GPIO_PUPD_PULLUP, USART1_TX_PIN | USART1_RX_PIN);
    gpio_output_options_set(USART1_TX_PORT, GPIO_OTYPE_PP, GPIO_OSPEED_50MHZ, USART1_TX_PIN | USART1_RX_PIN);

    usart_deinit(USART1);
    usart_baudrate_set(USART1, 115200U);
    usart_receive_config(USART1, USART_RECEIVE_ENABLE);
    usart_transmit_config(USART1, USART_TRANSMIT_ENABLE);
    usart_dma_receive_config(USART1, USART_RECEIVE_DMA_ENABLE);
    usart_enable(USART1);
}

void bsp_usart2_init(void)
{
    dma_single_data_parameter_struct dma_init_struct;

    rcu_periph_clock_enable(RCU_DMA0);
    rcu_periph_clock_enable(RCU_GPIOD);
    rcu_periph_clock_enable(RCU_USART2);

    dma_deinit(USART2_RX_DMA_PERIPH, USART2_RX_DMA_CHANNEL);
    dma_init_struct.direction = DMA_PERIPH_TO_MEMORY;
    dma_init_struct.memory0_addr = (uint32_t)usart2_rxbuffer;
    dma_init_struct.memory_inc = DMA_MEMORY_INCREASE_ENABLE;
    dma_init_struct.number = sizeof(usart2_rxbuffer);
    dma_init_struct.periph_addr = USART2_RDATA_ADDRESS;
    dma_init_struct.periph_inc = DMA_PERIPH_INCREASE_DISABLE;
    dma_init_struct.periph_memory_width = DMA_PERIPH_WIDTH_8BIT;
    dma_init_struct.priority = DMA_PRIORITY_ULTRA_HIGH;
    dma_single_data_mode_init(USART2_RX_DMA_PERIPH, USART2_RX_DMA_CHANNEL, &dma_init_struct);

    dma_circulation_disable(USART2_RX_DMA_PERIPH, USART2_RX_DMA_CHANNEL);
    dma_channel_subperipheral_select(USART2_RX_DMA_PERIPH, USART2_RX_DMA_CHANNEL, USART2_RX_DMA_SUBPERI);
    dma_channel_enable(USART2_RX_DMA_PERIPH, USART2_RX_DMA_CHANNEL);

    gpio_af_set(USART2_TX_PORT, AF_USART2, USART2_TX_PIN | USART2_RX_PIN);
    gpio_mode_set(USART2_TX_PORT, GPIO_MODE_AF, GPIO_PUPD_PULLUP, USART2_TX_PIN | USART2_RX_PIN);
    gpio_output_options_set(USART2_TX_PORT, GPIO_OTYPE_PP, GPIO_OSPEED_50MHZ, USART2_TX_PIN | USART2_RX_PIN);

    usart_deinit(USART2);
    usart_baudrate_set(USART2, 115200U);
    usart_receive_config(USART2, USART_RECEIVE_ENABLE);
    usart_transmit_config(USART2, USART_TRANSMIT_ENABLE);
    usart_dma_receive_config(USART2, USART_RECEIVE_DMA_ENABLE);
    usart_enable(USART2);
}

void bsp_usart5_init(void)
{
    dma_single_data_parameter_struct dma_init_struct;

    rcu_periph_clock_enable(RCU_DMA1);
    rcu_periph_clock_enable(RCU_GPIOC);
    rcu_periph_clock_enable(RCU_USART5);

    dma_deinit(USART5_RX_DMA_PERIPH, USART5_RX_DMA_CHANNEL);
    dma_init_struct.direction = DMA_PERIPH_TO_MEMORY;
    dma_init_struct.memory0_addr = (uint32_t)usart5_rxbuffer;
    dma_init_struct.memory_inc = DMA_MEMORY_INCREASE_ENABLE;
    dma_init_struct.number = sizeof(usart5_rxbuffer);
    dma_init_struct.periph_addr = USART5_RDATA_ADDRESS;
    dma_init_struct.periph_inc = DMA_PERIPH_INCREASE_DISABLE;
    dma_init_struct.periph_memory_width = DMA_PERIPH_WIDTH_8BIT;
    dma_init_struct.priority = DMA_PRIORITY_ULTRA_HIGH;
    dma_single_data_mode_init(USART5_RX_DMA_PERIPH, USART5_RX_DMA_CHANNEL, &dma_init_struct);

    dma_circulation_disable(USART5_RX_DMA_PERIPH, USART5_RX_DMA_CHANNEL);
    dma_channel_subperipheral_select(USART5_RX_DMA_PERIPH, USART5_RX_DMA_CHANNEL, USART5_RX_DMA_SUBPERI);
    dma_channel_enable(USART5_RX_DMA_PERIPH, USART5_RX_DMA_CHANNEL);

    gpio_af_set(USART5_TX_PORT, AF_USART5, USART5_TX_PIN | USART5_RX_PIN);
    gpio_mode_set(USART5_TX_PORT, GPIO_MODE_AF, GPIO_PUPD_PULLUP, USART5_TX_PIN | USART5_RX_PIN);
    gpio_output_options_set(USART5_TX_PORT, GPIO_OTYPE_PP, GPIO_OSPEED_50MHZ, USART5_TX_PIN | USART5_RX_PIN);

    usart_deinit(USART5);
    usart_baudrate_set(USART5, 115200U);
    usart_receive_config(USART5, USART_RECEIVE_ENABLE);
    usart_transmit_config(USART5, USART_TRANSMIT_ENABLE);
    usart_dma_receive_config(USART5, USART_RECEIVE_DMA_ENABLE);
    usart_enable(USART5);
}

void bsp_usart_all_init(void)
{
    bsp_usart0_init();
    bsp_usart1_init();
    bsp_usart2_init();
    bsp_usart5_init();
}

void bsp_oled_init(void)
{
    dma_single_data_parameter_struct dma_init_struct;
    /* enable GPIOB clock */
    rcu_periph_clock_enable(OLED_CLK_PORT);
    /* enable I2C0 clock */
    rcu_periph_clock_enable(RCU_I2C0);
    /* enable DMA0 clock */
    rcu_periph_clock_enable(RCU_DMA0);
    
    /* connect PB9 to I2C0_SDA */
    gpio_af_set(OLED_PORT, AF_OLED_I2C0, OLED_DAT_PIN);
    /* connect PB8 to I2C0_SCL */
    gpio_af_set(OLED_PORT, AF_OLED_I2C0, OLED_CLK_PIN);
    
    gpio_mode_set(OLED_PORT, GPIO_MODE_AF, GPIO_PUPD_PULLUP, OLED_DAT_PIN);
    gpio_output_options_set(OLED_PORT, GPIO_OTYPE_OD, GPIO_OSPEED_50MHZ, OLED_DAT_PIN);
    gpio_mode_set(OLED_PORT, GPIO_MODE_AF, GPIO_PUPD_PULLUP, OLED_CLK_PIN);
    gpio_output_options_set(OLED_PORT, GPIO_OTYPE_OD, GPIO_OSPEED_50MHZ, OLED_CLK_PIN);
    
    /* configure I2C0 clock */
    i2c_clock_config(I2C0, 400000, I2C_DTCY_2);
    /* configure I2C0 address */
    i2c_mode_addr_config(I2C0, I2C_I2CMODE_ENABLE, I2C_ADDFORMAT_7BITS, I2C0_OWN_ADDRESS7);
    /* enable I2C0 */
    i2c_enable(I2C0);
    /* enable acknowledge */
    i2c_ack_config(I2C0, I2C_ACK_ENABLE);
    
    /* Initialize DMA channel for I2C0 TX. */
    dma_deinit(DMA0, DMA_CH6);
    
    dma_single_data_para_struct_init(&dma_init_struct);
    dma_init_struct.direction = DMA_MEMORY_TO_PERIPH;
    dma_init_struct.memory0_addr = (uint32_t)oled_data_buf;  // Use OLED data buffer as the default source address.
    dma_init_struct.memory_inc = DMA_MEMORY_INCREASE_ENABLE;
    dma_init_struct.periph_memory_width = DMA_PERIPH_WIDTH_8BIT;
    dma_init_struct.number = 2;  // Send 2 bytes: control byte plus data/command.
    dma_init_struct.periph_addr = I2C0_DATA_ADDRESS;  // I2C0 data register address.
    dma_init_struct.periph_inc = DMA_PERIPH_INCREASE_DISABLE;
    dma_init_struct.priority = DMA_PRIORITY_ULTRA_HIGH;
    dma_single_data_mode_init(DMA0, DMA_CH6, &dma_init_struct);
    
    /* Configure DMA mode. */
    dma_circulation_disable(DMA0, DMA_CH6);
    dma_channel_subperipheral_select(DMA0, DMA_CH6, DMA_SUBPERI1);  // Subperipheral mapping for I2C0 TX.
}

void bsp_gd25qxx_init(void)
{
    rcu_periph_clock_enable(SPI_CLK_PORT);
    rcu_periph_clock_enable(SPI_CS_CLK_PORT);
    rcu_periph_clock_enable(RCU_SPI0);
    rcu_periph_clock_enable(RCU_DMA0);
    
    /* configure SPI1 GPIO */
    gpio_af_set(SPI_PORT, AF_SPI_FLASH, SPI_SCK | SPI_MISO | SPI_MOSI);
    gpio_mode_set(SPI_PORT, GPIO_MODE_AF, GPIO_PUPD_NONE, SPI_SCK | SPI_MISO | SPI_MOSI);
    gpio_output_options_set(SPI_PORT, GPIO_OTYPE_PP, GPIO_OSPEED_50MHZ, SPI_SCK | SPI_MISO | SPI_MOSI);

    /* set SPI1_NSS as GPIO*/
    gpio_mode_set(SPI_CS_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, SPI_NSS);
    gpio_output_options_set(SPI_CS_PORT, GPIO_OTYPE_PP, GPIO_OSPEED_50MHZ, SPI_NSS);
    
    spi_parameter_struct spi_init_struct;

    /* configure SPI1 parameter */
    spi_init_struct.trans_mode           = SPI_TRANSMODE_FULLDUPLEX;
    spi_init_struct.device_mode          = SPI_MASTER;
    spi_init_struct.frame_size           = SPI_FRAMESIZE_8BIT;
    spi_init_struct.clock_polarity_phase = SPI_CK_PL_HIGH_PH_2EDGE;
    spi_init_struct.nss                  = SPI_NSS_SOFT;
    spi_init_struct.prescale             = SPI_PSC_8;
    spi_init_struct.endian               = SPI_ENDIAN_MSB;
    spi_init(SPI0, &spi_init_struct);

    /* Initialize SPI Flash. */
    spi_flash_init();
}

void bsp_gd30ad3344_init(void)
{
    rcu_periph_clock_enable(SPI3_CLK_PORT);
    rcu_periph_clock_enable(RCU_SPI3);
    rcu_periph_clock_enable(RCU_DMA1);
    
    /* configure SPI0 GPIO */
    gpio_af_set(SPI3_PORT, AF_SPI3, SPI3_SCK | SPI3_MISO | SPI3_MOSI);
    gpio_mode_set(SPI3_PORT, GPIO_MODE_AF, GPIO_PUPD_NONE, SPI3_SCK | SPI3_MISO | SPI3_MOSI);
    gpio_output_options_set(SPI3_PORT, GPIO_OTYPE_PP, GPIO_OSPEED_50MHZ, SPI3_SCK | SPI3_MISO | SPI3_MOSI);

    /* set SPI0_NSS as GPIO*/
    gpio_mode_set(SPI3_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, SPI3_NSS);
    gpio_output_options_set(SPI3_PORT, GPIO_OTYPE_PP, GPIO_OSPEED_50MHZ, SPI3_NSS);
    
    spi_parameter_struct spi_init_struct;

    /* configure SPI1 parameter */
    spi_init_struct.trans_mode           = SPI_TRANSMODE_FULLDUPLEX;
    spi_init_struct.device_mode          = SPI_MASTER;
    spi_init_struct.frame_size           = SPI_FRAMESIZE_8BIT;
    spi_init_struct.clock_polarity_phase = SPI_CK_PL_LOW_PH_2EDGE;
    spi_init_struct.nss                  = SPI_NSS_SOFT;
    spi_init_struct.prescale             = SPI_PSC_8;
    spi_init_struct.endian               = SPI_ENDIAN_MSB;
    spi_init(SPI3, &spi_init_struct);

    /* Initialize SPI gd30ad3344. */
    GD30AD3344_Init();
}

void bsp_eth_init(void)
{
    rcu_periph_clock_enable(RCU_GPIOA);
    rcu_periph_clock_enable(RCU_GPIOB);
    rcu_periph_clock_enable(RCU_GPIOC);
    rcu_periph_clock_enable(RCU_GPIOD);

    rcu_periph_clock_enable(RCU_SYSCFG);
    rcu_periph_clock_enable(RCU_ENET);
    rcu_periph_clock_enable(RCU_ENETTX);
    rcu_periph_clock_enable(RCU_ENETRX);

    syscfg_enet_phy_interface_config(SYSCFG_ENET_PHY_RMII);

    gpio_af_set(ETH_REF_CLK_PORT, AF_ETH, ETH_REF_CLK_PIN | ETH_MDIO_PIN | ETH_CRS_DV_PIN);
    gpio_mode_set(ETH_REF_CLK_PORT, GPIO_MODE_AF, GPIO_PUPD_NONE, ETH_REF_CLK_PIN | ETH_MDIO_PIN | ETH_CRS_DV_PIN);
    gpio_output_options_set(ETH_REF_CLK_PORT, GPIO_OTYPE_PP, GPIO_OSPEED_50MHZ, ETH_REF_CLK_PIN | ETH_MDIO_PIN | ETH_CRS_DV_PIN);

    gpio_af_set(ETH_MDC_PORT, AF_ETH, ETH_MDC_PIN | ETH_RXD0_PIN | ETH_RXD1_PIN);
    gpio_mode_set(ETH_MDC_PORT, GPIO_MODE_AF, GPIO_PUPD_NONE, ETH_MDC_PIN | ETH_RXD0_PIN | ETH_RXD1_PIN);
    gpio_output_options_set(ETH_MDC_PORT, GPIO_OTYPE_PP, GPIO_OSPEED_50MHZ, ETH_MDC_PIN | ETH_RXD0_PIN | ETH_RXD1_PIN);

    gpio_af_set(ETH_TX_EN_PORT, AF_ETH, ETH_TX_EN_PIN | ETH_TXD0_PIN | ETH_TXD1_PIN);
    gpio_mode_set(ETH_TX_EN_PORT, GPIO_MODE_AF, GPIO_PUPD_NONE, ETH_TX_EN_PIN | ETH_TXD0_PIN | ETH_TXD1_PIN);
    gpio_output_options_set(ETH_TX_EN_PORT, GPIO_OTYPE_PP, GPIO_OSPEED_50MHZ, ETH_TX_EN_PIN | ETH_TXD0_PIN | ETH_TXD1_PIN);

    gpio_mode_set(PHY_RST_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, PHY_RST_PIN);
    gpio_output_options_set(PHY_RST_PORT, GPIO_OTYPE_PP, GPIO_OSPEED_50MHZ, PHY_RST_PIN);
    gpio_bit_set(PHY_RST_PORT, PHY_RST_PIN);
}

void bsp_sdio_init(void)
{
    rcu_periph_clock_enable(RCU_GPIOC);
    rcu_periph_clock_enable(RCU_GPIOD);
    rcu_periph_clock_enable(RCU_SDIO);

    gpio_af_set(SD_CLK_PORT, AF_SDIO, SD_DAT0_PIN | SD_DAT1_PIN | SD_DAT2_PIN | SD_DAT3_PIN | SD_CLK_PIN);
    gpio_af_set(SD_CMD_PORT, AF_SDIO, SD_CMD_PIN);

    gpio_mode_set(SD_CLK_PORT, GPIO_MODE_AF, GPIO_PUPD_PULLUP, SD_DAT0_PIN | SD_DAT1_PIN | SD_DAT2_PIN | SD_DAT3_PIN);
    gpio_output_options_set(SD_CLK_PORT, GPIO_OTYPE_PP, GPIO_OSPEED_25MHZ, SD_DAT0_PIN | SD_DAT1_PIN | SD_DAT2_PIN | SD_DAT3_PIN);

    gpio_mode_set(SD_CLK_PORT, GPIO_MODE_AF, GPIO_PUPD_NONE, SD_CLK_PIN);
    gpio_output_options_set(SD_CLK_PORT, GPIO_OTYPE_PP, GPIO_OSPEED_25MHZ, SD_CLK_PIN);

    gpio_mode_set(SD_CMD_PORT, GPIO_MODE_AF, GPIO_PUPD_PULLUP, SD_CMD_PIN);
    gpio_output_options_set(SD_CMD_PORT, GPIO_OTYPE_PP, GPIO_OSPEED_25MHZ, SD_CMD_PIN);
}

void bsp_adc_init(void)
{
    rcu_periph_clock_enable(ADC1_CLK_PORT);

    rcu_periph_clock_enable(RCU_ADC0);
    
    rcu_periph_clock_enable(RCU_DMA1);
    
    adc_clock_config(ADC_ADCCK_PCLK2_DIV8);
    
    /* config the GPIO as analog mode */
    gpio_mode_set(ADC1_PORT, GPIO_MODE_ANALOG, GPIO_PUPD_NONE, ADC1_PIN | ADC_VREF_PIN);
    
    /* ADC_DMA_channel configuration */
    dma_single_data_parameter_struct dma_single_data_parameter;

    /* ADC DMA_channel configuration */
    dma_deinit(DMA1, DMA_CH0);

    /* initialize DMA single data mode */
    dma_single_data_parameter.periph_addr = (uint32_t)(&ADC_RDATA(ADC0));
    dma_single_data_parameter.periph_inc = DMA_PERIPH_INCREASE_DISABLE;
    dma_single_data_parameter.memory0_addr = (uint32_t)(adc_value);
    dma_single_data_parameter.memory_inc = DMA_MEMORY_INCREASE_ENABLE;
    dma_single_data_parameter.periph_memory_width = DMA_PERIPH_WIDTH_16BIT;
    dma_single_data_parameter.direction = DMA_PERIPH_TO_MEMORY;
    dma_single_data_parameter.number = 2;
    dma_single_data_parameter.priority = DMA_PRIORITY_HIGH;
    dma_single_data_mode_init(DMA1, DMA_CH0, &dma_single_data_parameter);
    dma_channel_subperipheral_select(DMA1, DMA_CH0, DMA_SUBPERI0);

    /* enable DMA circulation mode */
    dma_circulation_enable(DMA1, DMA_CH0);

    /* enable DMA channel */
    dma_channel_enable(DMA1, DMA_CH0);
    
    /* ADC mode config */
    adc_sync_mode_config(ADC_SYNC_MODE_INDEPENDENT);
    /* ADC contineous function disable */
    adc_special_function_config(ADC0, ADC_CONTINUOUS_MODE, ENABLE);
    /* ADC scan mode disable */
    adc_special_function_config(ADC0, ADC_SCAN_MODE, ENABLE);
    /* ADC data alignment config */
    adc_data_alignment_config(ADC0, ADC_DATAALIGN_RIGHT);

    /* ADC channel length config */
    adc_channel_length_config(ADC0, ADC_ROUTINE_CHANNEL, 2);
    /* ADC routine channel config */
    adc_routine_channel_config(ADC0, 0, ADC_CHANNEL_10, ADC_SAMPLETIME_15);
    adc_routine_channel_config(ADC0, 1, ADC_CHANNEL_12, ADC_SAMPLETIME_15);
    /* ADC trigger config */
    adc_external_trigger_source_config(ADC0, ADC_ROUTINE_CHANNEL, ADC_EXTTRIG_ROUTINE_T0_CH0); 
    adc_external_trigger_config(ADC0, ADC_ROUTINE_CHANNEL, EXTERNAL_TRIGGER_DISABLE);

    /* ADC DMA function enable */
    adc_dma_request_after_last_enable(ADC0);
    adc_dma_mode_enable(ADC0);

    /* enable ADC interface */
    adc_enable(ADC0);
    /* wait for ADC stability */
    delay_1ms(1);
    /* ADC calibration and reset calibration */
    adc_calibration_enable(ADC0);

    /* enable ADC software trigger */
    adc_software_trigger_enable(ADC0, ADC_ROUTINE_CHANNEL);
}

void timer5_config(void)
{
    timer_parameter_struct timer_initpara;

    /* TIMER deinitialize */
    timer_deinit(TIMER5);

    /* TIMER configuration */
    timer_struct_para_init(&timer_initpara);
    timer_initpara.prescaler         = 239;
    timer_initpara.alignedmode       = TIMER_COUNTER_EDGE;
    timer_initpara.counterdirection  = TIMER_COUNTER_UP;
    timer_initpara.period            = 99;
    timer_initpara.clockdivision     = TIMER_CKDIV_DIV1;
    timer_initpara.repetitioncounter = 0;

    /* initialize TIMER init parameter struct */
    timer_init(TIMER5, &timer_initpara);

    /* TIMER master mode output trigger source: Update event */
    timer_master_output_trigger_source_select(TIMER5, TIMER_TRI_OUT_SRC_UPDATE);

    /* enable TIMER */
    timer_enable(TIMER5);
}

void bsp_dac_init(void)
{
    /* enable GPIOA clock */
    rcu_periph_clock_enable(DAC1_CLK_PORT);
    /* enable DAC clock */
    rcu_periph_clock_enable(RCU_DAC);
    /* enable TIMER clock */
    rcu_periph_clock_enable(RCU_TIMER5);

    /* configure PA4 as DAC output */
    gpio_mode_set(DAC1_PORT, GPIO_MODE_ANALOG, GPIO_PUPD_NONE, DAC1_PIN);

    /* initialize DAC */
    dac_deinit(DAC0);
    /* DAC trigger config */
    dac_trigger_source_config(DAC0, DAC_OUT0, DAC_TRIGGER_T5_TRGO);
    /* DAC trigger enable */
    dac_trigger_enable(DAC0, DAC_OUT0);
    /* DAC wave mode config */
    dac_wave_mode_config(DAC0, DAC_OUT0, DAC_WAVE_DISABLE);

    /* DAC enable */
    dac_enable(DAC0, DAC_OUT0);

    timer5_config();
}

int bsp_rtc_setup(void)
{
    int ret = 0;
    /* setup RTC time value */
    uint32_t tmp_hh = 0x23, tmp_mm = 0x59, tmp_ss = 0x50;

    rtc_initpara.factor_asyn = prescaler_a;
    rtc_initpara.factor_syn = prescaler_s;
    rtc_initpara.year = 0x25;
    rtc_initpara.day_of_week = RTC_SATURDAY;
    rtc_initpara.month = RTC_APR;
    rtc_initpara.date = 0x30;
    rtc_initpara.display_format = RTC_24HOUR;
    rtc_initpara.am_pm = RTC_AM;

    /* current time input */
    rtc_initpara.hour = tmp_hh;
    rtc_initpara.minute = tmp_mm;
    rtc_initpara.second = tmp_ss;

    /* RTC current time configuration */
    if(ERROR == rtc_init(&rtc_initpara)){
        ret = -1;
    }else{
        RTC_BKP0 = BKP_VALUE;
    }
    return ret;
}

void bsp_rtc_pre_cfg(void)
{
    #if defined (RTC_CLOCK_SOURCE_IRC32K)
          rcu_osci_on(RCU_IRC32K);
          rcu_osci_stab_wait(RCU_IRC32K);
          rcu_rtc_clock_config(RCU_RTCSRC_IRC32K);

          prescaler_s = 0x13F;
          prescaler_a = 0x63;
    #elif defined (RTC_CLOCK_SOURCE_LXTAL)
          rcu_osci_on(RCU_LXTAL);
          rcu_osci_stab_wait(RCU_LXTAL);
          rcu_rtc_clock_config(RCU_RTCSRC_LXTAL);

          prescaler_s = 0xFF;
          prescaler_a = 0x7F;
    #else
    #error RTC clock source should be defined.
    #endif /* RTC_CLOCK_SOURCE_IRC32K */

    rcu_periph_clock_enable(RCU_RTC);
    rtc_register_sync_wait();
}

int bsp_rtc_init(void)
{
    int ret = 0;
    /* enable access to RTC registers in Backup domain */
    rcu_periph_clock_enable(RCU_PMU);
    pmu_backup_write_enable();

    bsp_rtc_pre_cfg();
    /* get RTC clock entry selection */
    RTCSRC_FLAG = GET_BITS(RCU_BDCTL, 8, 9);

    /* Always set the initial RTC time when backup battery retention is unavailable. */
    bsp_rtc_setup();
    
//    if((BKP_VALUE != RTC_BKP0) || (0x00 == RTCSRC_FLAG)){
//        /* backup data register value is not correct or not yet programmed
//        or RTC clock source is not configured (when the first time the program 
//        is executed or data in RCU_BDCTL is lost due to Vbat feeding) */
//        ret = bsp_rtc_setup();
//    }else{
//        /* detect the reset source */
//        if (RESET != rcu_flag_get(RCU_FLAG_PORRST)){
//            ret = 1;
//        }else if (RESET != rcu_flag_get(RCU_FLAG_EPRST)){
//            ret = 2;
//        }
//    }

    rcu_all_reset_flag_clear();
    return ret;
}
