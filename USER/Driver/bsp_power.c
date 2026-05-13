#include "bsp_power.h"
#include "sd_app.h"

/*
 * 函数作用：
 *   在进入深度睡眠前关闭所有串口和对应 DMA 接收链路。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   无返回值。
 * 说明：
 *   必须先关中断、再关 DMA、最后关串口外设，避免睡眠前残留接收动作。
 */
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

/*
 * 函数作用：
 *   在进入深度睡眠前关闭 OLED 和 I2C 总线。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   无返回值。
 */
static void bsp_oled_disable_for_deepsleep(void)
{
    OLED_Display_Off();

    i2c_dma_config(I2C0, I2C_DMA_OFF);
    dma_channel_disable(DMA0, DMA_CH6);
    i2c_disable(I2C0);

    gpio_mode_set(OLED_PORT, GPIO_MODE_ANALOG, GPIO_PUPD_NONE, OLED_DAT_PIN | OLED_CLK_PIN);
}

/*
 * 函数作用：
 *   在进入深度睡眠前关闭 SPI Flash 和 GD30AD3344 对应的 SPI/DMA。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   无返回值。
 * 说明：
 *   片选先拉高，确保外设在休眠期间不会误进入命令接收状态。
 */
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

/*
 * 函数作用：
 *   在进入深度睡眠前关闭 SDIO 外设。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   无返回值。
 * 说明：
 *   SD 卡组件内部自带 GPIO 初始化，因此唤醒后不需要额外保留旧的 bsp_sdio_init。
 */
static void bsp_sdio_disable_for_deepsleep(void)
{
    nvic_irq_disable(SDIO_IRQn);
    sdio_dma_disable();
    sdio_clock_disable();
    sd_power_off();
    sdio_deinit();
    dma_channel_disable(DMA1, DMA_CH3);
}

/*
 * 函数作用：
 *   将大部分 GPIO 切换为低功耗状态，减少深度睡眠期间漏电。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   无返回值。
 * 说明：
 *   这里保留唤醒按键为输入上拉，其余不再使用的引脚尽量切到模拟模式。
 */
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

    gpio_mode_set(KEYE_PORT, GPIO_MODE_ANALOG, GPIO_PUPD_NONE, KEY1_PIN | KEY2_PIN | KEY3_PIN | KEY4_PIN | KEY5_PIN);
    gpio_mode_set(KEYB_PORT, GPIO_MODE_ANALOG, GPIO_PUPD_NONE, KEY6_PIN);

    gpio_mode_set(USART0_TX_PORT, GPIO_MODE_ANALOG, GPIO_PUPD_NONE, USART0_TX_PIN);
    gpio_mode_set(USART0_RX_PORT, GPIO_MODE_ANALOG, GPIO_PUPD_NONE, USART0_RX_PIN);
    gpio_mode_set(USART1_TX_PORT, GPIO_MODE_ANALOG, GPIO_PUPD_NONE, USART1_TX_PIN);
    gpio_mode_set(USART1_RX_PORT, GPIO_MODE_ANALOG, GPIO_PUPD_NONE, USART1_RX_PIN);
    /*
     * PE8 同时接 MAX3485 的 DE 和 RE#。
     * 这里必须保持低电平推挽输出，而不是切到模拟输入；否则 485_CS 会悬空，
     * 示波器容易看到工频/环境耦合的正弦波，且收发器方向状态不确定。
     */
    bsp_rs485_direction_receive();
    gpio_mode_set(USART2_TX_PORT, GPIO_MODE_ANALOG, GPIO_PUPD_NONE, USART2_TX_PIN);
    gpio_mode_set(USART2_RX_PORT, GPIO_MODE_ANALOG, GPIO_PUPD_NONE, USART2_RX_PIN);
    gpio_mode_set(USART5_TX_PORT, GPIO_MODE_ANALOG, GPIO_PUPD_NONE, USART5_TX_PIN);
    gpio_mode_set(USART5_RX_PORT, GPIO_MODE_ANALOG, GPIO_PUPD_NONE, USART5_RX_PIN);

    gpio_mode_set(OLED_PORT, GPIO_MODE_ANALOG, GPIO_PUPD_NONE, OLED_DAT_PIN | OLED_CLK_PIN);

    gpio_mode_set(GD25QXX_SPI_GPIO_PORT,
                  GPIO_MODE_ANALOG,
                  GPIO_PUPD_NONE,
                  GD25QXX_SPI_SCK_PIN | GD25QXX_SPI_MISO_PIN | GD25QXX_SPI_MOSI_PIN);
    gpio_mode_set(GD25QXX_SPI_CS_GPIO_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, GD25QXX_SPI_CS_PIN);
    gpio_output_options_set(GD25QXX_SPI_CS_GPIO_PORT, GPIO_OTYPE_PP, GPIO_OSPEED_2MHZ, GD25QXX_SPI_CS_PIN);
    GPIO_BOP(GD25QXX_SPI_CS_GPIO_PORT) = GD25QXX_SPI_CS_PIN;

    gpio_mode_set(GD30AD3344_SPI_GPIO_PORT,
                  GPIO_MODE_ANALOG,
                  GPIO_PUPD_NONE,
                  GD30AD3344_SPI_SCK_PIN | GD30AD3344_SPI_MISO_PIN | GD30AD3344_SPI_MOSI_PIN);
    gpio_mode_set(GD30AD3344_SPI_GPIO_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, GD30AD3344_SPI_CS_PIN);
    gpio_output_options_set(GD30AD3344_SPI_GPIO_PORT,
                            GPIO_OTYPE_PP,
                            GPIO_OSPEED_2MHZ,
                            GD30AD3344_SPI_CS_PIN);
    GPIO_BOP(GD30AD3344_SPI_GPIO_PORT) = GD30AD3344_SPI_CS_PIN;

    gpio_mode_set(ADC1_PORT, GPIO_MODE_ANALOG, GPIO_PUPD_NONE, ADC1_PIN | ADC_VREF_PIN);
    gpio_mode_set(DAC1_PORT, GPIO_MODE_ANALOG, GPIO_PUPD_NONE, DAC1_PIN);

    gpio_mode_set(GPIOC, GPIO_MODE_ANALOG, GPIO_PUPD_NONE, GPIO_PIN_8 | GPIO_PIN_9 | GPIO_PIN_10 | GPIO_PIN_11 | GPIO_PIN_12);
    gpio_mode_set(GPIOD, GPIO_MODE_ANALOG, GPIO_PUPD_NONE, GPIO_PIN_2);

    gpio_mode_set(KEYA_PORT, GPIO_MODE_INPUT, GPIO_PUPD_PULLUP, KEYW_PIN);
}

/*
 * 函数作用：
 *   从深度睡眠唤醒后，重新恢复时钟、滴答和所有板级外设。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   无返回值。
 * 说明：
 *   这里的顺序与上电初始化保持一致，确保依赖关系正确恢复。
 */
static void bsp_deepsleep_reinit_after_wakeup(void)
{
    /*
     * WFI 被 EXTI0 唤醒后，全局中断已经处于打开状态。
     * SystemInit() 会短暂把 VTOR 恢复到默认 Flash 起始地址，因此这里先关中断，
     * 避免 SysTick 或外设中断在向量表切换窗口内跳到 BootLoader 的入口。
     */
    __disable_irq();

    SystemInit();

    /*
     * 当前工程作为 BootLoader App 运行，真实向量表在 0x0800D000。
     * GD32 标准库的 SystemInit() 会按默认工程假设把 VTOR 重新设回
     * 0x08000000；如果不立刻切回 App 向量表，后续 SysTick/EXTI/USART
     * 中断会从 BootLoader 向量表取入口，表现为“已经唤醒但回不来”。
     */
    boot_app_vector_table_init();

    SystemCoreClockUpdate();
    systick_config();
    update_perf_counter();

    /*
     * 时钟、SysTick 和 App 向量表已经恢复后再开中断。
     * 后续外设初始化即使产生中断，也会使用 App 自己的中断入口。
     */
    __enable_irq();

    bsp_led_init();
    bsp_btn_init();
    bsp_usart_init();
    bsp_oled_init();
    OLED_Init();
    bsp_adc_init();
    bsp_dac_init();
    bsp_gd25qxx_init();
    bsp_gd30ad3344_init();
    bsp_rtc_init();
    sd_fatfs_init();
}

/*
 * 函数作用：
 *   进入深度睡眠，并在唤醒后恢复系统运行。
 * 主要流程：
 *   1. 收拢外设和 GPIO。
 *   2. 配置唤醒中断。
 *   3. 暂停性能计数和 SysTick 中断。
 *   4. 进入 PMU 深度睡眠模式。
 *   5. 唤醒后重新初始化系统。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   无返回值。
 */
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

    /*
     * 唤醒中断已经配置好后，再清一次 NVIC pending，确保 WFI 等待的是
     * 之后按下 WK_UP 产生的新下降沿，而不是配置阶段残留的旧挂起位。
     */
    NVIC_ClearPendingIRQ(EXTI0_IRQn);

    __enable_irq();

    /*
     * 先关闭 low-driver 模式，优先验证外部 EXTI0 唤醒可靠性。
     * 若后续需要进一步压低功耗，再在硬件实测基础上评估是否重新启用
     * PMU_LOWDRIVER_ENABLE。
     */
    pmu_to_deepsleepmode(PMU_LDO_LOWPOWER, PMU_LOWDRIVER_DISABLE, WFI_CMD);

    bsp_deepsleep_reinit_after_wakeup();
}
