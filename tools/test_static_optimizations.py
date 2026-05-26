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


def main() -> None:
    """执行全部静态契约检查，任一契约缺失都会让脚本以非零状态退出。"""
    gd25 = read_text("HardWare/GD25QXX/gd25qxx.c")
    gd25_h = read_text("HardWare/GD25QXX/gd25qxx.h")
    gd30 = read_text("HardWare/GD30AD3344/gd30ad3344.c")
    gd30_h = read_text("HardWare/GD30AD3344/gd30ad3344.h")
    power = read_text("HardWare/POWER/bsp_power.c")
    power_h = read_text("HardWare/POWER/bsp_power.h")
    btn_app = read_text("Function/btn_app.c")
    scheduler = read_text("Function/scheduler.c")
    scheduler_h = read_text("Function/scheduler.h")
    system_all = read_text("HeaderFiles/system_all.h")
    uvproj = read_text("project/2026706296.uvprojx")
    pt100_app = read_text("Function/gd30ad3344_pt100_app.c")
    pt100_app_h = read_text("Function/gd30ad3344_pt100_app.h")
    systick = read_text("User/systick.c")
    oled_app = read_text("Function/oled_app.c")
    oled = read_text("HardWare/OLED/oled.c")
    oled_h = read_text("HardWare/OLED/oled.h")
    bsp_oled_h = read_text("HardWare/OLED/bsp_oled.h")
    rtc = read_text("HardWare/RTC/bsp_rtc.c")
    rtc_h = read_text("HardWare/RTC/bsp_rtc.h")
    doc = read_text("工程文档.md")

    require("SPI_FLASH_WAIT_TIMEOUT" in gd25, "GD25QXX 缺少 Flash/DMA 超时常量")
    require("spi_flash_wait_for_write_end" in gd25_h and "int " in gd25_h, "GD25QXX 写等待接口未返回状态")
    require("spi_flash_enter_deep_power_down" in gd25_h and "int " in gd25_h, "GD25QXX deep power-down 接口未返回状态")
    require("GD30AD3344_SPI_WAIT_TIMEOUT" in gd30, "GD30AD3344 缺少 SPI DMA 超时常量")
    require("GD30AD3344_Enter_LowPower" in gd30_h and "int " in gd30_h, "GD30AD3344 低功耗接口未返回状态")
    require("bsp_spi_disable_for_deepsleep" in power and "flash_sleep_ok" in power, "低功耗入口未处理 SPI 器件低功耗失败")
    require("void bsp_enter_sleep(void)" in power_h, "POWER 头文件未导出 Sleep 模式入口")
    require("void bsp_enter_standby(void)" in power_h, "POWER 头文件未导出 Standby 模式入口")
    require("void bsp_enter_sleep(void)" in power, "POWER 实现缺少 Sleep 模式入口")
    require("void bsp_enter_standby(void)" in power, "POWER 实现缺少 Standby 模式入口")
    require("pmu_to_sleepmode(WFI_CMD)" in power, "Sleep 模式未通过 PMU WFI 入口进入")
    require("bsp_sleep_mask_runtime_irqs" in power and "bsp_sleep_unmask_runtime_irqs" in power, "Sleep 模式未临时屏蔽并恢复运行态外设中断")
    require("pmu_wakeup_pin_enable()" in power, "Standby 模式未启用 PMU WKUP 唤醒脚")
    require("pmu_to_standbymode()" in power, "Standby 模式未调用 PMU standby 入口")
    require("bsp_wait_keyw_low_before_standby" in power, "Standby 模式进入前未等待 KEYW 拉低")
    require("PMU_FLAG_STANDBY" in scheduler and "rcu_periph_clock_enable(RCU_PMU)" in scheduler, "启动流程未打开 PMU 时钟并识别 Standby 唤醒标志")
    require("PMU_FLAG_RESET_STANDBY" in scheduler and "PMU_FLAG_RESET_WAKEUP" in scheduler, "启动流程识别 Standby 后未清除 PMU 标志")
    require("bsp_enter_sleep();" in btn_app, "KEY1 未接入 Sleep 模式入口")
    require("bsp_enter_deepsleep();" in btn_app, "KEY2 未接入 Deep-sleep 模式入口")
    require("bsp_enter_standby();" in btn_app, "KEY3 未接入 Standby 模式入口")

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
    require("static uint8_t oled_write_packet" in oled, "OLED 底层缺少统一 I2C/DMA 事务 helper")
    require("static void oled_set_position_buf" in oled, "OLED 底层缺少批量定位命令 helper")
    require("static void oled_show_str_8x6" in oled, "OLED_ShowStr 缺少 6x8 字符串批量渲染 helper")
    require("OLED_Write_data_buf(zeros" in oled, "OLED_Clear 未使用页级批量清屏")
    require("OLED_Write_data_buf(fill" in oled, "OLED_Allfill 未使用页级批量填充")
    require("OLED_Write_cmd_buf(pos_cmds" in oled, "OLED_Set_Position 未使用批量命令发送")
    require("OLED_Write_data_buf(row_buf" in oled, "OLED_ShowStr 未使用行级批量数据发送")
    require("oled_printf_diff_start" in oled_app, "oled_printf 缺少差异段起点计算")
    require("oled_printf_diff_end" in oled_app, "oled_printf 缺少差异段终点计算")

    require("bsp_rtc_wait_osci_stable" in rtc, "RTC 缺少晶振稳定超时 helper")
    require("RTC_CLOCK_FALLBACK_IRC32K_ENABLE" in rtc_h, "RTC 缺少 IRC32K fallback 开关")
    require("RCU_RTCSRC_IRC32K" in rtc, "RTC 未实现 IRC32K fallback 路径")

    require("GD30AD3344_PGA_0V256" in gd30 and "0.256" in gd30, "GD30AD3344 PGA 0.256V 映射缺失")
    require("GD30AD3344_PGA_0V064" in gd30 and "0.064" in gd30, "GD30AD3344 PGA 0.064V 映射缺失")
    require("PT100_FRONTEND_GAIN" in pt100_app and "16.41f" in pt100_app, "PT100 app 未按 R5=6.49k 配置前端增益")
    require("PT100_EXCITATION_CURRENT_A" in pt100_app and "0.001f" in pt100_app, "PT100 app 未配置 1mA 激励电流")
    require("GD30AD3344_Channel_4" in pt100_app, "PT100 app 未默认读取 AIN0~GND 通道")
    require("GD30AD3344_PGA_4V096" in pt100_app, "PT100 app 未默认使用 ±4.096V 量程")
    require("gd30ad3344_pt100_task" in pt100_app_h and "pt100_measurement_t" in pt100_app_h, "PT100 app 头文件缺少任务入口或测量结果类型")
    require("gd30ad3344_pt100_task" in scheduler, "调度器未注册 PT100 周期采样任务")
    require("gd30ad3344_pt100_app.h" in system_all, "聚合头未包含 PT100 app 头文件")
    require("gd30ad3344_pt100_app.c" in uvproj, "Keil 工程未包含 PT100 app 源文件")

    require("调度器唤醒重基线" in doc, "工程文档未同步调度器唤醒优化说明")
    require("RTC 补偿" in doc, "工程文档未同步 RTC 补偿说明")
    require("KEY1" in doc and "Sleep" in doc and "KEY2" in doc and "Deep-sleep" in doc and "KEY3" in doc and "Standby" in doc, "工程文档未同步三档低功耗按键映射")
    require("PMU WKUP" in doc and "KEYW" in doc, "工程文档未说明 Standby 使用 KEYW/PMU WKUP 唤醒")
    require("OLED 底层批量写入优化" in doc, "工程文档未同步 OLED 底层批量写入说明")
    require("OLED 二轮事务压缩优化" in doc, "工程文档未同步 OLED 二轮事务压缩说明")
    require("GD30AD3344 PT100 应用层" in doc, "工程文档未同步 GD30AD3344 PT100 app 说明")
    require("16.41" in doc and "1mA" in doc, "工程文档未说明 PT100 前端增益或激励电流")


if __name__ == "__main__":
    main()
