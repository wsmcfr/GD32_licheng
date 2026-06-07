"""静态检查正式 CIMC 工程裁剪契约是否仍成立。

该脚本不替代 Keil 编译和硬件实测，只用于快速发现旧调试输出、旧文件系统、
旧按键演示或旧 OTA 模块被重新引入的问题。
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


def require_missing(relative_path: str) -> None:
    """确认正式版已删除的源码文件没有被重新创建。"""
    require(not (ROOT / relative_path).exists(), f"正式版不应重新出现旧文件: {relative_path}")


def main() -> None:
    """执行正式版静态契约检查，任一契约缺失都会让脚本以非零状态退出。"""
    main_c = read_text("USER/main.c")
    scheduler = read_text("Function/scheduler.c")
    interrupts = read_text("USER/gd32f4xx_it.c")
    usart_h = read_text("Driver/USART/bsp_usart.h")
    usart_c = read_text("Driver/USART/bsp_usart.c")
    usart_app = read_text("Function/usart_app.c")
    usart_app_h = read_text("Function/usart_app.h")
    pt100_app = read_text("Function/gd30ad3344_pt100_app.c")
    adc_app = read_text("Function/adc_app.c")
    led_h = read_text("Driver/LED/bsp_led.h")
    oled_app = read_text("Function/oled_app.c")
    system_all = read_text("HeaderFiles/system_all.h")
    uvproj = read_text("project/2026706296.uvprojx")
    doc = read_text("工程文档.md")

    for relative_path in (
        "Function/btn_app.c",
        "Function/btn_app.h",
        "Function/uart_ota_app.c",
        "Function/uart_ota_app.h",
        "Protocol/ota_image_protocol.c",
        "Protocol/ota_image_protocol.h",
        "Driver/KEY/bsp_key.c",
        "Driver/KEY/bsp_key.h",
        "Driver/POWER/bsp_power.c",
        "Driver/POWER/bsp_power.h",
        "Driver/GD25QXX/gd25qxx.c",
        "Driver/GD25QXX/gd25qxx.h",
        "Driver/GD25QXX/smartfs_port.c",
        "Driver/GD25QXX/lfs.c",
        "Driver/GD25QXX/lfs_util.c",
        "Driver/GD25QXX/lfs_port.c",
        "tools/pack_ota_image.c",
        "tools/pack_ota_image.exe",
        "tools/test_header_bin_ota_static.py",
    ):
        require_missing(relative_path)

    combined_source = "\n".join(
        [
            main_c,
            scheduler,
            interrupts,
            usart_h,
            usart_c,
            usart_app,
            usart_app_h,
            pt100_app,
            adc_app,
            system_all,
            uvproj,
        ]
    )
    for forbidden in (
        "my_printf",
        "DEBUG_USART",
        "CIMC_DEBUG_LOG_ENABLE",
        "app_debug_usart_putc",
        "PT100:",
        "BOOT:",
        "Project_ota.bin",
        "pack_ota_image",
        "bsp_usart0_init",
        "bsp_usart5_init",
        "USART0_IRQHandler",
        "USART5_IRQHandler",
        "usart0_rxbuffer",
        "usart5_rxbuffer",
    ):
        require(forbidden not in combined_source, f"正式版源码仍包含已删除调试/旧功能标记: {forbidden}")

    require("__use_no_semihosting" in main_c, "main.c 未保留 ARMCLANG no-semihosting 标记")
    require("_sys_open" in main_c and "_sys_write" in main_c and "_sys_exit" in main_c, "main.c 缺少 C 库 retarget 空桩")
    require("_ttywrch" in main_c and "return 0;" in main_c, "main.c 缺少直接丢弃输出的 _ttywrch/_sys_write 行为")

    require("RS485_PORT               USART1" in usart_h, "USART 正式接口不是 USART1")
    require("RS485_BAUD            19200U" in usart_h, "USART1/RS485 默认波特率不是 19200")
    require("RS485_DIR_PIN                  GPIO_PIN_8" in usart_h, "RS485 方向控制脚 PE8 契约缺失")
    require("usart_baudrate_set(USART1, RS485_BAUD)" in usart_c, "USART 初始化未收敛到 USART1/RS485 默认波特率")
    require("proto_rx(fbuf, clen)" in usart_app, "USART App 未接入 CIMC 协议解析入口")

    require("PT100_OFFSET_V" in pt100_app, "PT100 app 缺少模块 Vout 零点偏置")
    require("PT100_SLOPE_V_OHM" in pt100_app, "PT100 app 缺少模块 Vout-电阻斜率")
    require("resistance_to_temp" in pt100_app, "PT100 app 未通过分段线性插值换算温度")
    require("GD30AD3344_AD_Read(PT100_ADC_CHANNEL, PT100_ADC_PGA, &adc_v)" in pt100_app, "PT100 app 未检查 GD30AD3344 采样返回状态")

    require("adc_app_set_dac_raw" in adc_app and "dac_data_set" in adc_app, "DAC 0x0301 控制入口缺失")
    for forbidden_dac_pattern in (
        "convertarr[0] = adc_value[0]",
        "dac_data_set(DAC0, DAC_OUT0, DAC_ALIGN_12B_R, adc_value[0])",
    ):
        require(forbidden_dac_pattern not in adc_app, "ADC app 仍可能把 ADC 采样直通覆盖 DAC")

    require("LED1_PIN" in led_h and "LED2_PIN" in led_h, "正式版 LED1/LED2 定义缺失")
    require("LED3" not in led_h and "LED4" not in led_h and "LED5" not in led_h and "LED6" not in led_h, "LED 头文件仍保留多余 LED")
    require("AutoSample" in oled_app and "IDLE" in oled_app, "OLED app 未保留正式双行状态显示")

    for forbidden_entry in (
        "btn_app.c",
        "uart_ota_app.c",
        "ota_image_protocol.c",
        "bsp_key.c",
        "bsp_power.c",
        "smartfs_port.c",
        "lfs.c",
        "lfs_util.c",
        "gd25qxx.c",
        "Project_ota.bin",
        "pack_ota_image",
    ):
        require(forbidden_entry not in uvproj, f"Keil 工程仍包含旧正式禁用项: {forbidden_entry}")

    require("Protocol\\cimc_protocol.c" in uvproj, "Keil 工程未编译 CIMC 协议文件")
    require("Function\\cimc_status.c" in uvproj, "Keil 工程未编译 CIMC 状态文件")
    require("Function\\gd30ad3344_pt100_app.c" in uvproj, "Keil 工程未编译 PT100 app")
    require("USART1/RS485" in doc and "19200" in doc, "工程文档未同步正式 USART1/RS485 19200 口径")
    require("App 不提供调试输出 API" in doc, "工程文档未说明正式版调试输出 API 已删除")


if __name__ == "__main__":
    main()
