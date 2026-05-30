"""静态检查 YModem OTA 接入是否满足当前工程契约。

该脚本不替代 Keil 编译和硬件实测，只用于在没有固件单元测试框架时，
快速发现 YModem 接收模块漏接调度、缓冲区尺寸不足或文档未同步等回归。
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
    """执行全部 YModem OTA 静态契约检查。"""
    bsp_usart_h = read_text("HardWare/USART/bsp_usart.h")
    system_all = read_text("HeaderFiles/system_all.h")
    uart_ota = read_text("Function/uart_ota_app.c")
    uart_ota_h = read_text("Function/uart_ota_app.h")
    uvproj = read_text("project/2026706296.uvprojx")
    ota_doc = read_text("BootLoader_App_实际升级运行流程详解.md")
    spec = read_text(".trellis/spec/backend/embedded-ota-guidelines.md")

    ymodem_c = read_text("Function/uart_ota_ymodem.c")
    ymodem_h = read_text("Function/uart_ota_ymodem.h")

    require("#define BSP_USART1_RX_BUFFER_SIZE      1152U" in bsp_usart_h,
            "USART1 DMA 缓冲必须大于 YModem 1K 帧总长度 1029 字节")
    require("#define UART_OTA_YMODEM_PACKET_1K_SIZE        1024U" in ymodem_c,
            "YModem 模块缺少 1K 数据块大小常量")
    require("#define UART_OTA_YMODEM_FRAME_1K_TOTAL_SIZE" in ymodem_c,
            "YModem 模块缺少 1K 完整帧长度常量")
    require("UART_OTA_YMODEM_SOH" in ymodem_c and "UART_OTA_YMODEM_STX" in ymodem_c,
            "YModem 模块缺少 SOH/STX 帧头识别")
    require("UART_OTA_YMODEM_EOT" in ymodem_c and "UART_OTA_YMODEM_CRC_REQ" in ymodem_c,
            "YModem 模块缺少 EOT 或 CRC 请求控制字符")
    require("prv_uart_ota_ymodem_crc16" in ymodem_c,
            "YModem 模块缺少 CRC16 校验函数")
    require("bootloader_port_prepare_download_area" in ymodem_c,
            "YModem START 包未准备下载缓存区")
    require("bootloader_port_write_download_chunk" in ymodem_c,
            "YModem DATA 包未写入下载缓存区")
    require("bootloader_port_write_upgrade_info" in ymodem_c,
            "YModem 完成后未写 BootLoader 参数区")
    require("bootloader_port_request_upgrade_reset" not in ymodem_c,
            "YModem 模块不应直接复位，复位应由 uart_ota_task 统一执行")

    require("uart_ota_ymodem_reset_runtime" in uart_ota,
            "uart_ota_reset_runtime 未复位 YModem 状态")
    require("uart_ota_ymodem_try_process_packet" in uart_ota,
            "uart_ota_task 未调用 YModem 解析入口")
    require("UART_OTA_RESULT_SUCCESS == ymodem_result" in uart_ota,
            "uart_ota_task 未在 YModem 成功后触发统一复位流程")
    require("uart_ota_ymodem_send_poll" in uart_ota,
            "uart_ota_task 未周期发送 YModem CRC 请求字符")
    require("uart_ota_ymodem.h" in uart_ota_h,
            "uart_ota_app.h 未包含 YModem 公共声明")

    require("uart_ota_ymodem.h" in system_all,
            "聚合头未包含 YModem OTA 头文件")
    require("uart_ota_ymodem.c" in uvproj,
            "Keil 工程未包含 YModem OTA 源文件")
    require("YModem" in ota_doc and "纸飞机调试助手" in ota_doc,
            "OTA 用户文档未说明纸飞机调试助手 YModem 发送 Project.bin")
    require("BootLoader 端不需要修改" in ota_doc,
            "OTA 用户文档未说明第一版 YModem 不改 BootLoader")
    require("YModem" in spec and "BSP_USART1_RX_BUFFER_SIZE` | `1152U`" in spec,
            "Trellis OTA 规范未同步 YModem 和新缓冲区大小")


if __name__ == "__main__":
    main()
