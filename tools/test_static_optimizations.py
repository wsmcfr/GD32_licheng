"""静态检查本轮低功耗与计时优化是否落到关键代码契约。

该脚本不替代 Keil 编译和硬件实测，只用于在没有固件单元测试框架时，
快速发现“优化点漏改、接口漏接、等待仍无超时”等回归。
"""

from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def read_text(relative_path: str) -> str:
    """读取仓库内文本文件，统一使用 UTF-8 以兼容中文注释。"""
    return (ROOT / relative_path).read_text(encoding="utf-8")


def require(condition: bool, message: str) -> None:
    """对静态契约做断言，失败时抛出带中文说明的 AssertionError。"""
    if not condition:
        raise AssertionError(message)


def require_order(text: str, before: str, after: str, message: str) -> None:
    """断言一段源码中 before 片段必须出现在 after 片段之前。

    参数说明：
        text：需要检查的源码全文。
        before：应当先出现的源码片段。
        after：应当后出现的源码片段。
        message：断言失败时输出的中文错误信息。
    返回值说明：
        无返回值；断言失败时抛出 AssertionError。
    """
    before_index = text.find(before)
    after_index = text.find(after)

    require(before_index >= 0 and after_index >= 0 and before_index < after_index, message)


def main() -> None:
    """执行全部静态契约检查，任一契约缺失都会让脚本以非零状态退出。"""
    gd25 = read_text("Driver/GD25QXX/gd25qxx.c")
    gd25_h = read_text("Driver/GD25QXX/gd25qxx.h")
    gd30 = read_text("Driver/GD30AD3344/gd30ad3344.c")
    gd30_h = read_text("Driver/GD30AD3344/gd30ad3344.h")
    power = read_text("Driver/POWER/bsp_power.c")
    power_h = read_text("Driver/POWER/bsp_power.h")
    btn_app = read_text("Function/btn_app.c")
    led_app = read_text("Function/led_app.c")
    led_app_h = read_text("Function/led_app.h")
    scheduler = read_text("Function/scheduler.c")
    scheduler_h = read_text("Function/scheduler.h")
    interrupts = read_text("User/gd32f4xx_it.c")
    libopt = read_text("User/gd32f4xx_libopt.h")
    system_all = read_text("HeaderFiles/system_all.h")
    uvproj = read_text("project/2026706296.uvprojx")
    pt100_app = read_text("Function/gd30ad3344_pt100_app.c")
    pt100_app_h = read_text("Function/gd30ad3344_pt100_app.h")
    analog = read_text("Driver/ANALOG/bsp_analog.c")
    analog_h = read_text("Driver/ANALOG/bsp_analog.h")
    usart = read_text("Driver/USART/bsp_usart.c")
    uart_ota = read_text("Function/uart_ota_app.c")
    systick = read_text("User/systick.c")
    oled_app = read_text("Function/oled_app.c")
    oled = read_text("Driver/OLED/oled.c")
    oled_h = read_text("Driver/OLED/oled.h")
    bsp_oled_h = read_text("Driver/OLED/bsp_oled.h")
    rtc = read_text("Driver/RTC/bsp_rtc.c")
    rtc_h = read_text("Driver/RTC/bsp_rtc.h")
    usart_app = read_text("Function/usart_app.c")
    doc = read_text("工程文档.md")

    require("SPI_FLASH_WAIT_TIMEOUT" in gd25, "GD25QXX 缺少 Flash/DMA 超时常量")
    require("spi_flash_wait_for_write_end" in gd25_h and "int " in gd25_h, "GD25QXX 写等待接口未返回状态")
    require("int spi_flash_sector_erase" in gd25_h, "GD25QXX 扇区擦除接口未向上返回状态")
    require("int spi_flash_page_write" in gd25_h, "GD25QXX 页写接口未向上返回状态")
    require("int spi_flash_buffer_write" in gd25_h, "GD25QXX 缓冲写接口未向上返回状态")
    require("int spi_flash_write_enable" in gd25_h, "GD25QXX 写使能接口未向上返回状态")
    require("return spi_flash_wait_for_write_end();" in gd25, "GD25QXX 写擦接口未传播 WIP/DMA 等待失败")
    require("prv_spi_flash_send_command_byte" in gd25 and "if(0 != prv_spi_flash_send_command_byte" in gd25, "GD25QXX 写擦命令阶段 DMA 错误未上抛")
    require("SMART_STORAGE_ERR_IO" in read_text("Driver/GD25QXX/smartfs_port.c"), "SMARTFS 未把底层 Flash 写擦失败映射为 IO 错误")
    require("spi_flash_enter_deep_power_down" in gd25_h and "int " in gd25_h, "GD25QXX deep power-down 接口未返回状态")
    require("GD30AD3344_SPI_WAIT_TIMEOUT" in gd30, "GD30AD3344 缺少 SPI DMA 超时常量")
    require("GD30AD3344_Enter_LowPower" in gd30_h and "int " in gd30_h, "GD30AD3344 低功耗接口未返回状态")
    require("int GD30AD3344_AD_Read" in gd30_h, "GD30AD3344 采样接口未返回错误状态")
    require("GD30AD3344_GetLastError" in gd30_h and "GD30AD3344_GetLastError" in gd30, "GD30AD3344 缺少最近一次采样错误查询接口")
    require("bsp_spi_disable_for_deepsleep" in power and "flash_sleep_ok" in power, "低功耗入口未处理 SPI 器件低功耗失败")
    require("void bsp_enter_sleep(void)" in power_h, "POWER 头文件未导出 Sleep 模式入口")
    require("void bsp_enter_standby(void)" in power_h, "POWER 头文件未导出 Standby 模式入口")
    require("void bsp_enter_sleep(void)" in power, "POWER 实现缺少 Sleep 模式入口")
    require("void bsp_enter_standby(void)" in power, "POWER 实现缺少 Standby 模式入口")
    require("pmu_to_sleepmode(WFI_CMD)" in power, "Sleep 模式未通过 PMU WFI 入口进入")
    require("bsp_sleep_mask_runtime_irqs" in power and "bsp_sleep_unmask_runtime_irqs" in power, "Sleep 模式未临时屏蔽并恢复运行态外设中断")
    require("SDIO_IRQn" not in power and "RCU_SDIO" not in power, "POWER 仍保留已删除 SDIO 的低功耗处理")
    require("pmu_wakeup_pin_enable()" in power, "Standby 模式未启用 PMU WKUP 唤醒脚")
    require("pmu_to_standbymode()" in power, "Standby 模式未调用 PMU standby 入口")
    require("bsp_wait_keyw_low_before_standby" not in power, "Standby 模式仍要求 KEYW 参与进入流程")
    require("bsp_wait_key4_release_before_standby" in power, "Standby 模式未等待 KEY4 松开确认")
    require_order(
        power,
        "bsp_oled_preblank_for_standby();",
        "bsp_wait_key4_release_before_standby();",
        "Standby 等待 KEY4 确认前未先关闭 OLED 显示",
    )
    require_order(
        power,
        "bsp_standby_preblank_indicators();",
        "bsp_wait_key4_release_before_standby();",
        "Standby 等待 KEY4 确认前未先关闭 LED 指示",
    )
    require_order(
        power,
        "gpio_mode_set(KEYA_PORT, GPIO_MODE_ANALOG, GPIO_PUPD_NONE, KEY5_PIN | KEY6_PIN);",
        "gpio_mode_set(KEYA_PORT, GPIO_MODE_INPUT, GPIO_PUPD_PULLUP, KEY4_PIN);",
        "Standby GPIO 收拢未保留 KEY4 为确认键输入",
    )
    require("PMU_FLAG_STANDBY" in scheduler and "rcu_periph_clock_enable(RCU_PMU)" in scheduler, "启动流程未打开 PMU 时钟并识别 Standby 唤醒标志")
    require("PMU_FLAG_RESET_STANDBY" in scheduler and "PMU_FLAG_RESET_WAKEUP" in scheduler, "启动流程识别 Standby 后未清除 PMU 标志")
    require("bsp_enter_sleep();" in btn_app, "KEY1 未接入 Sleep 模式入口")
    require("bsp_enter_deepsleep();" in btn_app, "KEY2 未接入 Deep-sleep 模式入口")
    require("bsp_enter_standby();" in btn_app, "KEY3 未接入 Standby 模式入口")
    require("LED3_TOGGLE;\n        /*\n         * KEY3" not in btn_app, "KEY3 进入 Standby 前仍会翻转 LED3")
    require("LED1_TOGGLE" not in btn_app and "LED2_TOGGLE" not in btn_app and "LED4_TOGGLE" not in btn_app, "按键 app 仍直接翻转 LED 硬件，未走 led_app 状态源")
    require("led_app_toggle" in led_app_h and "led_app_set" in led_app_h, "LED app 头文件缺少统一设置/翻转接口")
    require("led_app_all_off" in led_app_h and "led_app_blank_for_sleep" in led_app_h, "LED app 缺少低功耗熄灯接口")
    require("led_app_reset_cache" in led_app_h and "g_led_cache_valid" in led_app, "LED app 缺少硬件刷新缓存复位能力")
    require("led_app_toggle(0U)" in btn_app and "led_app_toggle(1U)" in btn_app, "KEY1/KEY2 未通过 led_app_toggle 更新 ucLed")
    require("led_app_blank_for_sleep();" in power, "低功耗 GPIO 收拢前未通过 LED app 熄灯并复位刷新缓存")
    require("led_app_all_off();" in power, "Standby 确认等待前未通过 LED app 统一关闭应用层 LED 状态")
    require("led_app_reset_cache();" in power, "唤醒恢复后未复位 LED app 刷新缓存")
    require("LED1_OFF" not in power and "LED2_OFF" not in power and "LED6_OFF" not in power, "POWER 层仍直接操作 LEDx_OFF，可能绕过 ucLed/cache 状态")

    require("scheduler_reset_runtime" in scheduler_h, "调度器头文件未导出唤醒重基线接口")
    require("void scheduler_reset_runtime(void)" in scheduler, "调度器实现缺少唤醒重基线接口")
    require("scheduler_reset_runtime();" in power, "唤醒恢复后未重置调度器运行时基线")

    require("timebase_adjust_ms" in systick, "timebase 缺少睡眠补偿接口")
    require("bsp_rtc_get_epoch_seconds" in rtc_h, "RTC 头文件未导出秒级时间戳接口")
    require("sleep_epoch" in power and "timebase_adjust_ms" in power, "深睡前后未用 RTC 补偿 timebase")

    require("OLED_APP_LINE_BUFFER_SIZE" in oled_app, "OLED app 缺少行缓冲大小常量")
    require("char buffer[OLED_APP_LINE_BUFFER_SIZE]" in oled_app, "oled_printf 栈缓冲未缩小到行缓冲")
    require("g_oled_line_cache" in oled_app, "OLED app 缺少脏行缓存")
    require("OLED_TX_BUFFER_SIZE" in bsp_oled_h, "OLED BSP 缺少批量 DMA 发送缓冲大小常量")
    require("OLED_Write_data_buf" in oled_h, "OLED 头文件缺少批量数据写入接口声明")
    require("OLED_Write_cmd_buf" in oled_h, "OLED 头文件缺少批量命令写入接口声明")
    require("OLED_I2C_BUSY_WAIT_MS 10000U" not in oled, "OLED 总线忙等待仍为 10000ms 级别，异常屏会长时间卡住调度器")
    require("uint8_t OLED_Write_cmd_buf" in oled_h and "uint8_t OLED_Write_data_buf" in oled_h, "OLED 批量写接口未向上返回 I2C/DMA 成败")
    require("uint8_t OLED_ShowStr" in oled_h and "uint8_t OLED_ShowStr" in oled, "OLED_ShowStr 未向 oled_printf 返回刷新成败")
    require("static uint8_t oled_write_packet" in oled, "OLED 底层缺少统一 I2C/DMA 事务 helper")
    require("static uint8_t oled_set_position_buf" in oled, "OLED 底层缺少可返回状态的批量定位命令 helper")
    require("static uint8_t oled_show_str_8x6" in oled, "OLED_ShowStr 缺少可返回状态的 6x8 字符串批量渲染 helper")
    require("OLED_Write_data_buf(zeros" in oled, "OLED_Clear 未使用页级批量清屏")
    require("OLED_Write_data_buf(fill" in oled, "OLED_Allfill 未使用页级批量填充")
    require("OLED_Write_cmd_buf(pos_cmds" in oled, "OLED_Set_Position 未使用批量命令发送")
    require("OLED_Write_data_buf(row_buf" in oled, "OLED_ShowStr 未使用行级批量数据发送")
    require("oled_printf_diff_start" in oled_app, "oled_printf 缺少差异段起点计算")
    require("oled_printf_diff_end" in oled_app, "oled_printf 缺少差异段终点计算")
    require("if(0U != OLED_ShowStr" in oled_app, "oled_printf 未根据 OLED_ShowStr 成功结果更新行缓存")
    require("屏幕刷新成功后才更新缓存" in oled_app, "oled_printf 缺少避免 OLED 缓存假成功的说明")

    require("bsp_rtc_wait_osci_stable" in rtc, "RTC 缺少晶振稳定超时 helper")
    require("RTC_CLOCK_FALLBACK_IRC32K_ENABLE" in rtc_h, "RTC 缺少 IRC32K fallback 开关")
    require("RCU_RTCSRC_IRC32K" in rtc, "RTC 未实现 IRC32K fallback 路径")
    require("bsp_rtc_try_restore_lxtal_from_irc32k" in rtc, "RTC 缺少从 IRC32K 自动迁回 LXTAL 的恢复 helper")
    require("rcu_bkp_reset_enable()" in rtc and "rcu_bkp_reset_disable()" in rtc, "RTC 迁回 LXTAL 时未复位备份域清除旧 RTCSRC")
    require("RTC_STATUS_SOURCE_LXTAL" in rtc_h and "RTC_STATUS_SOURCE_IRC32K" in rtc_h, "RTC 头文件缺少可诊断的时钟源枚举")
    require("bsp_rtc_get_status" in rtc_h and "bsp_rtc_get_status" in rtc, "RTC 缺少状态诊断接口")
    require("rtcstat" in usart_app and "prv_uart_handle_rtcstat" in usart_app, "USART0 缺少 rtcstat RTC 状态诊断命令")
    require("RTC: STAT" in usart_app and "src=%s" in usart_app and "psc_a" in usart_app and "psc_s" in usart_app, "rtcstat 输出缺少时钟源或分频诊断字段")

    require("GD30AD3344_PGA_0V256" in gd30 and "0.256" in gd30, "GD30AD3344 PGA 0.256V 映射缺失")
    require("GD30AD3344_PGA_0V064" in gd30 and "0.064" in gd30, "GD30AD3344 PGA 0.064V 映射缺失")
    require("PT100_COMMERCIAL_OFFSET_V" in pt100_app and "0.94235185f" in pt100_app, "PT100 app 未配置商业版模块 Vout 零点偏置")
    require("PT100_COMMERCIAL_RESISTANCE_SLOPE_V_PER_OHM" in pt100_app and "0.0019314815f" in pt100_app, "PT100 app 未配置商业版模块 Vout-电阻斜率")
    require("pt100_calibration_point_t" in pt100_app and "s_pt100_temperature_table" in pt100_app, "PT100 app 未配置测试板电阻-温度插值表")
    require("prv_pt100_resistance_to_temperature" in pt100_app and "PT100_TEMPERATURE_TABLE_COUNT" in pt100_app, "PT100 app 未通过分段线性插值换算温度")
    require("154.0f" in pt100_app and "141.11f" in pt100_app and "80.6f" in pt100_app and "-49.27f" in pt100_app, "PT100 app 插值表缺少测试板边界标定点")
    require("PT100_COMMERCIAL_TEMPERATURE_GAIN" not in pt100_app and "PT100_COMMERCIAL_TEMPERATURE_OFFSET_C" not in pt100_app, "PT100 app 仍保留全局线性温度公式")
    require("PT100_FRONTEND_GAIN" not in pt100_app and "PT100_EXCITATION_CURRENT_A" not in pt100_app, "PT100 app 仍保留工业版前端增益/激励电流换算")
    require("GD30AD3344_Channel_4" in pt100_app, "PT100 app 未默认读取 AIN0~GND 通道")
    require("GD30AD3344_PGA_4V096" in pt100_app, "PT100 app 未默认使用 ±4.096V 量程")
    require("GD30AD3344_AD_Read(PT100_ADC_CHANNEL, PT100_ADC_PGA, &adc_voltage_v)" in pt100_app, "PT100 app 未检查 GD30AD3344 采样返回状态")
    require("PT100: sample failed" in pt100_app, "PT100 app 未在 GD30 采样失败时输出明确日志")
    require("my_printf(DEBUG_USART" in pt100_app and "PT100:" in pt100_app, "PT100 app 未在任务末尾通过 USART0 打印测量结果")
    require("PT100_DEBUG_LOG_PERIOD_MS" in pt100_app and "prv_pt100_should_log" in pt100_app, "PT100 app 串口日志未做周期节流")
    require("if(prv_pt100_should_log())" in pt100_app, "PT100 app 测量/失败日志未统一通过节流判断")
    require("gd30ad3344_pt100_task" in pt100_app_h and "pt100_measurement_t" in pt100_app_h, "PT100 app 头文件缺少任务入口或测量结果类型")
    require("gd30ad3344_pt100_task" in scheduler, "调度器未注册 PT100 周期采样任务")
    require("gd30ad3344_pt100_app.h" in system_all, "聚合头未包含 PT100 app 头文件")
    require("gd30ad3344_pt100_app.c" in uvproj, "Keil 工程未包含 PT100 app 源文件")
    require("sd_app" not in scheduler and "SD_FATFS" not in scheduler, "启动流程仍保留 SD/FatFs 初始化或测试")
    require("SDIO_IRQHandler" not in interrupts and "sd_interrupts_process" not in interrupts, "中断文件仍保留 SDIO 中断入口")
    require("sdio_sdcard.h" not in system_all and "diskio.h" not in system_all and "ff.h" not in system_all and "sd_app.h" not in system_all, "聚合头仍包含 SD/FatFs 头文件")
    require("gd32f4xx_sdio.h" not in libopt, "标准库选项头仍启用 SDIO 外设头")
    require("sd_app.c" not in uvproj and "sdio_sdcard.c" not in uvproj and "fat_fs" not in uvproj and "gd32f4xx_sdio.c" not in uvproj, "Keil 工程仍包含 SD/FatFs/SDIO 源文件或路径")

    require("__IO uint16_t adc_value[2]" in analog_h and "__IO uint16_t adc_value[2]" in analog, "ADC DMA 采样缓冲 adc_value 未声明为 volatile/__IO")
    require("dma_single_data_para_struct_init(&dma_single_data_parameter)" in analog, "ADC DMA 初始化未先套用标准默认模板")
    require(usart.count("dma_single_data_para_struct_init(&dma_init_struct)") >= 3, "USART0/USART1/USART5 DMA 初始化未全部先套用标准默认模板")
    require(gd25.count("dma_single_data_para_struct_init(&dma_init_struct)") >= 3, "GD25QXX SPI DMA 临时配置未全部先套用标准默认模板")
    require(gd30.count("dma_single_data_para_struct_init(&dma_init_struct)") >= 3, "GD30AD3344 SPI DMA 临时配置未全部先套用标准默认模板")
    require("UART_OTA_TASK_DRAIN_LIMIT" in uart_ota and "drained_count" in uart_ota, "OTA 任务未在单次调度内限额排空 DMA 队列")
    require("while(drained_count < UART_OTA_TASK_DRAIN_LIMIT)" in uart_ota, "OTA 任务仍可能每 5ms 只消费一个 DMA 队列槽")

    require("调度器唤醒重基线" in doc, "工程文档未同步调度器唤醒优化说明")
    require("RTC 补偿" in doc, "工程文档未同步 RTC 补偿说明")
    require("rtcstat" in doc and "RTCSRC" in doc and "IRC32K" in doc, "工程文档未同步 RTC 时钟源诊断和 IRC32K 恢复说明")
    require("KEY1" in doc and "Sleep" in doc and "KEY2" in doc and "Deep-sleep" in doc and "KEY3" in doc and "Standby" in doc, "工程文档未同步三档低功耗按键映射")
    require("PMU WKUP" in doc and "KEYW" in doc, "工程文档未说明 Standby 使用 KEYW/PMU WKUP 唤醒")
    require("OLED 底层批量写入优化" in doc, "工程文档未同步 OLED 底层批量写入说明")
    require("OLED 二轮事务压缩优化" in doc, "工程文档未同步 OLED 二轮事务压缩说明")
    require("OLED 失败不上缓存" in doc, "工程文档未同步 OLED 失败返回与缓存一致性说明")
    require("LED 应用层状态源" in doc, "工程文档未同步 LED 状态统一入口说明")
    require("OTA 队列排空" in doc, "工程文档未同步 OTA 队列排空优化说明")
    require("PT100 串口日志节流" in doc, "工程文档未同步 PT100 日志节流说明")
    require("ADC DMA 缓冲区为 __IO" in doc, "工程文档未同步 ADC DMA volatile 说明")
    require("GD30AD3344 PT100 应用层" in doc, "工程文档未同步 GD30AD3344 PT100 app 说明")
    require("0.94235185" in doc and "0.0019314815" in doc and "分段线性插值" in doc and "141.11" in doc, "工程文档未同步商业版 PT100 分段插值公式")
    require("SD_FATFS" not in doc and "fat_fs" not in doc and "FatFs" not in doc and "SDIO" not in doc and "SD 卡" not in doc, "工程文档仍保留 SD/FatFs 用户说明")


if __name__ == "__main__":
    main()
