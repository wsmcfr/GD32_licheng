#include "bsp_power.h"
#include "btn_app.h"
#include "scheduler.h"
#include "oled_app.h"
#include "uart_ota_app.h"
#include "led_app.h"

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
    nvic_irq_disable(DMA0_Channel5_IRQn);
    usart_dma_receive_config(USART1, USART_RECEIVE_DMA_DISABLE);
    dma_interrupt_disable(USART1_RX_DMA_PERIPH, USART1_RX_DMA_CHANNEL, DMA_INT_FTF);
    dma_channel_disable(USART1_RX_DMA_PERIPH, USART1_RX_DMA_CHANNEL);
    usart_disable(USART1);

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
 *   在 Standby 流程等待 KEY4 松开确认之前，先让 OLED 真正息屏。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   无返回值。
 * 说明：
 *   Standby 需要用户按 KEY4 确认后才继续收拢。如果先等待 KEY4、后关 OLED，
 *   屏幕会在等待阶段停留在最后一帧并继续发光。这里仅发送 SSD1306 关显示
 *   和关电荷泵命令，不关闭 I2C/DMA/GPIO；后续仍由 bsp_oled_disable_for_deepsleep()
 *   统一收拢总线和引脚，避免破坏既有深睡资源关闭顺序。
 */
static void bsp_oled_preblank_for_standby(void)
{
    OLED_Display_Off();
}

/*
 * 函数作用：
 *   在 Standby 确认等待前关闭所有 LED 指示。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   无返回值。
 * 说明：
 *   KEY3 进入最深睡眠后，用户看到的状态应立即进入“准备睡眠”的暗屏暗灯状态。
 *   后续 bsp_gpio_enter_deepsleep_state() 仍会再次关闭 LED 并收拢 GPIO，
 *   这里提前执行是为了覆盖等待 KEY4 松开确认期间的可见亮灯问题。
 */
static void bsp_standby_preblank_indicators(void)
{
    led_app_all_off();
}

/*
 * 函数作用：
 *   在进入深度睡眠前关闭 SPI Flash 和 GD30AD3344 对应的 SPI/DMA。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   0：表示 Flash 和 GD30AD3344 都已经成功下发低功耗命令。
 *  -1：表示至少一个器件低功耗命令失败，后续仍会关闭总线和 GPIO 以继续收拢功耗。
 * 说明：
 *   片选先拉高，确保外设在休眠期间不会误进入命令接收状态。
 */
static int bsp_spi_disable_for_deepsleep(void)
{
    int flash_sleep_ok;
    int gd30_sleep_ok;

    /*
     * 板上没有给 SPI Flash / GD30AD3344 做物理断电，因此在关 SPI 总线前先让
     * 两颗芯片各自进入芯片级待机，避免 MCU 睡下去后它们仍保持正常待机电流。
     * 若器件或 DMA 异常导致低功耗命令失败，驱动会超时返回；这里继续关闭总线，
     * 避免因为一个外设异常而卡死在“准备休眠”阶段。
     */
    flash_sleep_ok = spi_flash_enter_deep_power_down();
    gd30_sleep_ok = GD30AD3344_Enter_LowPower();

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

    if((0 != flash_sleep_ok) || (0 != gd30_sleep_ok)) {
        return -1;
    }

    return 0;
}

/*
 * 函数作用：
 *   在进入深度睡眠前关闭不再需要的外设时钟，减少仅靠“外设 disable”遗留的时钟树功耗。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   无返回值。
 * 说明：
 *   这里保留 PMU、RTC、唤醒键所在 GPIOA 与唤醒配置相关时钟，优先保证本轮
 *   低功耗优化仍然保持稳定唤醒；其余已被收拢的串口、SPI、I2C、DMA、ADC、
 *   DAC、TIMER、LED/普通按键 GPIO 时钟统一关闭。
 */
static void bsp_clock_disable_for_deepsleep(void)
{
    rcu_periph_clock_disable(RCU_USART0);
    rcu_periph_clock_disable(RCU_USART1);
    rcu_periph_clock_disable(RCU_USART5);
    rcu_periph_clock_disable(RCU_I2C0);
    rcu_periph_clock_disable(RCU_SPI0);
    rcu_periph_clock_disable(RCU_SPI3);
    rcu_periph_clock_disable(RCU_ADC0);
    rcu_periph_clock_disable(RCU_DAC);
    rcu_periph_clock_disable(RCU_TIMER5);
    rcu_periph_clock_disable(RCU_DMA0);
    rcu_periph_clock_disable(RCU_DMA1);
    rcu_periph_clock_disable(RCU_GPIOB);
    rcu_periph_clock_disable(RCU_GPIOC);
    rcu_periph_clock_disable(RCU_GPIOD);
    rcu_periph_clock_disable(RCU_GPIOE);
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

    led_app_blank_for_sleep();

    /*
     * 普通按键在深睡前统一切到模拟输入，减少无用数字输入泄漏。
     * 但 KEYW 仍需保留为 PA0 上拉输入，供后续 EXTI0/PMU WKUP 唤醒链路使用；
     * KEY4 在 Standby 流程里作为“松开确认”键，进入真正 Standby 前保持输入。
     */
    gpio_mode_set(KEYB_PORT, GPIO_MODE_ANALOG, GPIO_PUPD_NONE, KEY1_PIN);
    gpio_mode_set(KEYC_PORT, GPIO_MODE_ANALOG, GPIO_PUPD_NONE, KEY2_PIN | KEY3_PIN);
    gpio_mode_set(KEYA_PORT, GPIO_MODE_ANALOG, GPIO_PUPD_NONE, KEY5_PIN | KEY6_PIN);
    gpio_mode_set(KEYA_PORT, GPIO_MODE_INPUT, GPIO_PUPD_PULLUP, KEY4_PIN);

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

    gpio_mode_set(KEYA_PORT, GPIO_MODE_INPUT, GPIO_PUPD_PULLUP, KEYW_PIN);
}

/*
 * 函数作用：
 *   在进入 Standby 前等待 KEY4 被用户释放，作为最深睡眠的确认动作。
 * 主要流程：
 *   1. 确保 KEY4/PA7 仍为上拉输入。
 *   2. 先等待 KEY4 进入按下低电平，避免直接把松开态误判为确认。
 *   3. 再等待 KEY4 松开为高电平并稳定达到消抖时间。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   无返回值。
 * 说明：
 *   KEYW/PA0 是 Standby 唤醒键，不再参与进入睡眠的确认流程。
 *   这里用 KEY4 的“按下后松开”作为确认，避免 KEY3 误触后立即继续关断外设。
 */
static void bsp_wait_key4_release_before_standby(void)
{
    uint32_t stable_release_ms = 0U;

    rcu_periph_clock_enable(KEYA_CLK_PORT);
    gpio_mode_set(KEYA_PORT, GPIO_MODE_INPUT, GPIO_PUPD_PULLUP, KEY4_PIN);

    while(KEY4_READ) {
        delay_ms(1U);
    }

    while(stable_release_ms < 20U) {
        if(KEY4_READ) {
            stable_release_ms++;
        } else {
            stable_release_ms = 0U;
        }
        delay_ms(1U);
    }
}

/*
 * 函数作用：
 *   临时屏蔽 Sleep 期间不希望唤醒 CPU 的运行态外设中断。
 * 主要流程：
 *   1. 关闭调试串口 USART0 和 RS485/OTA USART1 的 NVIC 中断。
 *   2. 清除这些中断的挂起状态，避免刚执行 WFI 就被旧事件唤醒。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   无返回值。
 * 说明：
 *   Sleep 模式本身会被任意已使能中断唤醒。当前 KEY1 语义要求由 KEYW 唤醒，
 *   因此只保留 EXTI0 作为本轮明确唤醒源。
 */
static void bsp_sleep_mask_runtime_irqs(void)
{
    nvic_irq_disable(USART0_IRQn);
    nvic_irq_disable(USART1_IRQn);
    nvic_irq_disable(DMA0_Channel5_IRQn);

    NVIC_ClearPendingIRQ(USART0_IRQn);
    NVIC_ClearPendingIRQ(USART1_IRQn);
    NVIC_ClearPendingIRQ(DMA0_Channel5_IRQn);
}

/*
 * 函数作用：
 *   恢复 Sleep 前临时屏蔽的运行态外设中断。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   无返回值。
 * 说明：
 *   这里按当前工程默认运行态恢复 USART0 和 USART1 中断优先级。
 *   若后续新增可在 Sleep 期间保留的唤醒源，应同步调整屏蔽和恢复列表。
 */
static void bsp_sleep_unmask_runtime_irqs(void)
{
    NVIC_ClearPendingIRQ(USART0_IRQn);
    NVIC_ClearPendingIRQ(USART1_IRQn);
    NVIC_ClearPendingIRQ(DMA0_Channel5_IRQn);

    nvic_irq_enable(USART0_IRQn, 0U, 0U);
    nvic_irq_enable(USART1_IRQn, 1U, 0U);
    nvic_irq_enable(DMA0_Channel5_IRQn, 1U, 1U);
}

/*
 * 函数作用：
 *   根据睡前和醒后的 RTC 秒级时间戳补偿本地毫秒 timebase。
 * 主要流程：
 *   1. 判断睡前时间戳是否有效。
 *   2. 读取当前 RTC 秒级时间戳。
 *   3. 只在醒后时间不早于睡前时间时追加毫秒补偿。
 *   4. 对超过 32 位毫秒表达范围的极长睡眠做分段补偿。
 * 参数说明：
 *   sleep_epoch：睡前通过 RTC 读取到的秒级时间戳，单位为秒。
 *   sleep_epoch_valid：sleep_epoch 是否有效，非 0 表示可用于补偿。
 * 返回值说明：
 *   无返回值。
 */
static void bsp_apply_rtc_sleep_compensation(uint32_t sleep_epoch, uint8_t sleep_epoch_valid)
{
    uint32_t wake_epoch;
    uint32_t sleep_elapsed_s;

    if((0U == sleep_epoch_valid) || (0 != bsp_rtc_get_epoch_seconds(&wake_epoch))) {
        return;
    }

    if(wake_epoch < sleep_epoch) {
        /*
         * RTC 被重新设置或备份域异常时，醒后秒计数可能小于睡前值。
         * 这种情况下不能用负向差值补偿 timebase，直接保留当前运行时基。
         */
        return;
    }

    /*
     * SysTick 在低功耗期间停止，RTC 仍然以秒级推进。
     * 这里用 RTC 秒差补偿本地 timebase，保证跨睡眠日志时间和超时基准不被缩短。
     */
    sleep_elapsed_s = wake_epoch - sleep_epoch;
    while(sleep_elapsed_s > (0xFFFFFFFFUL / 1000UL)) {
        /*
         * 极长休眠超过单次 32 位毫秒补偿能力时分段追加。
         * 每段都小于 2^32ms，timebase_adjust_ms() 可以正确处理低 32 位回绕。
         */
        timebase_adjust_ms((0xFFFFFFFFUL / 1000UL) * 1000UL);
        sleep_elapsed_s -= (0xFFFFFFFFUL / 1000UL);
    }
    timebase_adjust_ms(sleep_elapsed_s * 1000UL);
}

/*
 * 函数作用：
 *   执行轻量 Sleep 唤醒后的运行时状态恢复。
 * 主要流程：
 *   1. 重新建立 SysTick/DWT timebase。
 *   2. 用 RTC 秒差补偿 Sleep 期间停止的本地 tick。
 *   3. 重置按键和调度器运行基线，避免唤醒后误消费睡前按键状态。
 *   4. 关闭仅用于本次 Sleep 的 EXTI0 唤醒通道。
 * 参数说明：
 *   sleep_epoch：睡前通过 RTC 读取到的秒级时间戳，单位为秒。
 *   sleep_epoch_valid：sleep_epoch 是否有效，非 0 表示可用于补偿。
 * 返回值说明：
 *   无返回值。
 */
static void bsp_sleep_recover_after_wakeup(uint32_t sleep_epoch, uint8_t sleep_epoch_valid)
{
    timebase_update_after_clock_change();
    bsp_apply_rtc_sleep_compensation(sleep_epoch, sleep_epoch_valid);
    bsp_sleep_unmask_runtime_irqs();
    app_btn_init();
    scheduler_reset_runtime();
    bsp_wkup_key_exti_deinit();
}

/*
 * 函数作用：
 *   从深度睡眠唤醒后，重新恢复时钟、滴答和所有板级外设。
 * 参数说明：
 *   sleep_epoch：睡前通过 RTC 读取到的秒级时间戳，单位为秒。
 *   sleep_epoch_valid：sleep_epoch 是否有效，非 0 表示可用于唤醒后补偿 timebase。
 * 返回值说明：
 *   无返回值。
 * 说明：
 *   这里的顺序与上电初始化保持一致，确保依赖关系正确恢复。
 *   本地 timebase 已经接管系统节拍，恢复时先重建 SysTick，再按 RTC 秒差补偿睡眠时间。
 */
static void bsp_deepsleep_reinit_after_wakeup(uint32_t sleep_epoch, uint8_t sleep_epoch_valid)
{
    /*
     * WFI 被 EXTI0 唤醒后，全局中断已经处于打开状态。
     * SystemInit() 会短暂把 VTOR 恢复到默认 Flash 起始地址，因此这里先关中断，
     * 避免 SysTick 或外设中断在向量表切换窗口内跳到 BootLoader 的入口。
     */
    __disable_irq();

    SystemInit();

    /*
     * 当前工程作为 BootLoader App 运行，真实向量表在 0x08011000。
     * GD32 标准库的 SystemInit() 会按默认工程假设把 VTOR 重新设回
     * 0x08000000；如果不立刻切回 App 向量表，后续 SysTick/EXTI/USART
     * 中断会从 BootLoader 向量表取入口，表现为“已经唤醒但回不来”。
     */
    boot_app_vector_table_init();

    SystemCoreClockUpdate();
    timebase_update_after_clock_change();
    bsp_apply_rtc_sleep_compensation(sleep_epoch, sleep_epoch_valid);

    /*
     * 时钟、SysTick 和 App 向量表已经恢复后再开中断。
     * 后续外设初始化即使产生中断，也会使用 App 自己的中断入口。
     */
    __enable_irq();

    bsp_led_init();
    led_app_reset_cache();
    bsp_btn_init();
    bsp_usart_init();
    /*
     * OTA 会话运行态也要在唤醒后清空，避免睡前残留的半包长度或标志位
     * 被误当成新的 OTA 帧继续处理。
     */
    uart_ota_reset_runtime();
    bsp_oled_init();
    OLED_Init();
    oled_app_reset_cache();
    bsp_adc_init();
    bsp_dac_init();
    bsp_gd25qxx_init();
    /*
     * SPI0/GPIO/DMA 时钟恢复后，再发送 release 指令唤醒 Flash 本体。
     * 若在总线资源尚未重建前发命令，SPI 寄存器和片选 GPIO 还不可用，释放动作
     * 实际不会落到器件上，后续第一次访问就可能拿到无效响应。
     */
    if(0 != spi_flash_release_from_deep_power_down()) {
        /*
         * release 指令失败通常表示 SPI 链路异常。这里不在唤醒路径停机，
         * 后续 SMARTFS/Flash 访问会继续按各自错误处理路径暴露问题。
         */
    }
    bsp_gd30ad3344_init();
    bsp_rtc_init();
    /*
     * 简化按键方案把边沿检测状态保存在 btn_app 静态变量中。
     * 深睡唤醒后 GPIO 已经重新初始化，因此这里同步重置按键模块状态，
     * 避免沿用睡前缓存导致第一次按键被误判为旧状态延续。
     */
    app_btn_init();

    /*
     * 唤醒恢复已经完成，所有周期任务从当前 tick 重新计时。
     * 这样可以避免 OLED/UART/RTC/ADC 在恢复后的第一轮主循环同时集中运行。
     */
    scheduler_reset_runtime();

    /*
     * EXTI0 只在深睡阶段作为 WK_UP 唤醒源使用。
     * 系统恢复到正常运行态后立即收口，避免 PA0 后续作为普通按键输入时
     * 每次下降沿都额外打进一次低功耗专用中断。
     */
    bsp_wkup_key_exti_deinit();
}

/*
 * 函数作用：
 *   进入 Sleep 轻量睡眠模式，并在 KEYW/PA0 唤醒后继续运行。
 * 主要流程：
 *   1. 保存睡前 RTC 秒级时间戳，供唤醒后补偿本地 timebase。
 *   2. 配置 KEYW/PA0 的 EXTI0 下降沿中断作为本轮 Sleep 唤醒源。
 *   3. 停止 SysTick，避免 1ms tick 立即把 CPU 唤醒。
 *   4. 调用 `pmu_to_sleepmode(WFI_CMD)` 停止 CPU。
 *   5. 唤醒后恢复 timebase、按键扫描基线和调度器运行基线。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   无返回值。
 */
void bsp_enter_sleep(void)
{
    uint32_t sleep_epoch = 0U;
    uint8_t sleep_epoch_valid = 0U;

    rcu_periph_clock_enable(RCU_PMU);

    /*
     * Sleep 不关闭 RTC 和大部分外设，但会暂停 SysTick。
     * 提前记录 RTC 秒级时间戳后，唤醒阶段可以把停止的 tick 时间补回来。
     */
    if(0 == bsp_rtc_get_epoch_seconds(&sleep_epoch)) {
        sleep_epoch_valid = 1U;
    }

    __disable_irq();
    bsp_sleep_mask_runtime_irqs();
    bsp_wkup_key_exti_init();
    pmu_flag_clear(PMU_FLAG_RESET_WAKEUP);
    timebase_prepare_reconfiguration();

    /*
     * 配置完 EXTI 后再次清 NVIC pending，保证 WFI 等待的是用户后续按下 KEYW
     * 产生的新下降沿，而不是配置过程或按键抖动留下的旧事件。
     */
    NVIC_ClearPendingIRQ(EXTI0_IRQn);
    __enable_irq();

    pmu_to_sleepmode(WFI_CMD);

    /*
     * Sleep 只停 CPU，不破坏系统时钟和外设配置，因此唤醒后不需要重跑
     * SystemInit() 或外设初始化，只恢复被主动暂停的本地运行时状态。
     */
    bsp_sleep_recover_after_wakeup(sleep_epoch, sleep_epoch_valid);
}

/*
 * 函数作用：
 *   进入深度睡眠，并在唤醒后恢复系统运行。
 * 主要流程：
 *   1. 收拢外设和 GPIO。
 *   2. 配置唤醒中断。
 *   3. 暂停本地 timebase 和 SysTick 中断。
 *   4. 进入 PMU 深度睡眠模式。
 *   5. 唤醒后重新初始化系统，并用 RTC 秒差补偿深睡期间停止的 timebase。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   无返回值。
 */
void bsp_enter_deepsleep(void)
{
    int spi_sleep_result;
    uint32_t sleep_epoch = 0U;
    uint8_t sleep_epoch_valid = 0U;

    rcu_periph_clock_enable(RCU_PMU);

    /*
     * 进入深睡前先保存 RTC 秒级时间戳。后续关闭串口和 SysTick 后无法再依赖日志或
     * 运行时 tick 估算睡眠时长，因此这里尽量提前取样；失败时只跳过补偿。
     */
    if(0 == bsp_rtc_get_epoch_seconds(&sleep_epoch)) {
        sleep_epoch_valid = 1U;
    }

    __disable_irq();

    bsp_usart_disable_for_deepsleep();
    bsp_oled_disable_for_deepsleep();
    spi_sleep_result = bsp_spi_disable_for_deepsleep();
    if(0 != spi_sleep_result) {
        /*
         * USART 已关闭，不能再输出现场日志。低功耗命令失败时不阻塞睡眠流程，
         * 继续执行 DMA/SPI/GPIO/时钟收拢，避免外设异常把系统卡在入睡前。
         */
    }
    adc_disable(ADC0);
    adc_dma_mode_disable(ADC0);
    dma_channel_disable(DMA1, DMA_CH0);
    dac_disable(DAC0, DAC_OUT0);
    dac_dma_disable(DAC0, DAC_OUT0);
    dma_channel_disable(DMA0, DMA_CH5);
    timer_disable(TIMER5);

    bsp_gpio_enter_deepsleep_state();
    bsp_wkup_key_exti_init();
    bsp_clock_disable_for_deepsleep();

    pmu_flag_clear(PMU_FLAG_RESET_WAKEUP);
    pmu_flag_clear(PMU_FLAG_RESET_STANDBY);

    timebase_prepare_reconfiguration();

    /*
     * 唤醒中断已经配置好后，再清一次 NVIC pending，确保 WFI 等待的是
     * 之后按下 WK_UP 产生的新下降沿，而不是配置阶段残留的旧挂起位。
     */
    NVIC_ClearPendingIRQ(EXTI0_IRQn);

    __enable_irq();

    /* 本轮进入低功耗优化阶段，尝试启用 low-driver 进一步压低 MCU 深睡电流。 */
    pmu_to_deepsleepmode(PMU_LDO_LOWPOWER, PMU_LOWDRIVER_ENABLE, WFI_CMD);

    bsp_deepsleep_reinit_after_wakeup(sleep_epoch, sleep_epoch_valid);
}

/*
 * 函数作用：
 *   收拢板级外设后进入 Standby 待机模式。
 * 主要流程：
 *   1. 关闭串口、OLED、SPI、ADC、DAC、TIMER 等外设。
 *   2. 将 GPIO 收拢为低漏电状态，并保留 KEYW/PA0 的 WKUP 引脚条件。
 *   3. 清除 PMU 唤醒/待机标志，启用 PMU WKUP 引脚。
 *   4. 调用 `pmu_to_standbymode()` 进入 Standby。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   无返回值；Standby 唤醒会走复位启动流程，正常情况下本函数不会返回。
 * 说明：
 *   当前 KEYW 原理图为上拉、按下接地。PMU WKUP 为高电平/上升沿唤醒，
 *   因此 KEYW 只作为 Standby 唤醒键使用，不再作为进入 Standby 的确认键。
 */
void bsp_enter_standby(void)
{
    int spi_sleep_result;

    rcu_periph_clock_enable(RCU_PMU);

    /*
     * KEY3 只负责进入最深睡眠准备态，KEYW/PA0 只负责唤醒。
     * 这里先熄灭 OLED 和 LED，再等待 KEY4 的按下后松开动作作为确认；
     * 等待动作必须放在关闭串口和 SysTick 前，保证 delay_ms() 仍可用于稳定消抖。
     */
    bsp_oled_preblank_for_standby();
    bsp_standby_preblank_indicators();
    bsp_wait_key4_release_before_standby();

    __disable_irq();

    bsp_usart_disable_for_deepsleep();
    bsp_oled_disable_for_deepsleep();
    spi_sleep_result = bsp_spi_disable_for_deepsleep();
    if(0 != spi_sleep_result) {
        /*
         * Standby 前调试串口已经关闭，即使外设低功耗命令失败，也继续收拢
         * 其他资源，避免在无法输出日志的路径里永久停住。
         */
    }
    adc_disable(ADC0);
    adc_dma_mode_disable(ADC0);
    dma_channel_disable(DMA1, DMA_CH0);
    dac_disable(DAC0, DAC_OUT0);
    dac_dma_disable(DAC0, DAC_OUT0);
    dma_channel_disable(DMA0, DMA_CH5);
    timer_disable(TIMER5);

    bsp_gpio_enter_deepsleep_state();
    bsp_clock_disable_for_deepsleep();

    pmu_wakeup_pin_disable();
    pmu_flag_clear(PMU_FLAG_RESET_WAKEUP);
    pmu_flag_clear(PMU_FLAG_RESET_STANDBY);
    pmu_wakeup_pin_enable();

    timebase_prepare_reconfiguration();

    /*
     * Standby 唤醒后不会回到当前调用栈，因此不需要配置 EXTI0 或恢复调度器。
     * 这里只重新开全局中断，让 PMU 的 WFI 入口按标准流程进入待机状态。
     */
    __enable_irq();

    pmu_to_standbymode();

    /*
     * 若调试器或选项字导致 Standby 没有真正进入，这里用复位回到明确的启动态，
     * 避免在外设已关闭、timebase 已停用的半初始化环境中继续执行。
     */
    NVIC_SystemReset();
}
