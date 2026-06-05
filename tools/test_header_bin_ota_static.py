import binascii
import os
import struct
import subprocess
import tempfile
import unittest
from pathlib import Path


class HeaderBinOtaStaticTest(unittest.TestCase):
    """
    测试作用：
      覆盖“OTA 头部 + App payload”方案的静态契约和本机打包工具。
    说明：
      这些测试不依赖硬件，只验证源码中必须存在的新协议常量、旧协议删除结果，
      以及 C 打包工具生成的 Project_ota.bin 是否符合 App 侧解析格式。
    """

    repo_root = Path(__file__).resolve().parents[1]
    expected_boot_magic = "0xC0DEF47A"
    expected_param_addr = "0x08010000"
    expected_app_addr = "0x08011000"
    expected_backup_addr = "0x08031000"
    expected_download_addr = "0x08051000"
    expected_region_size_kb = "128"
    expected_app_region_size = "0x00020000"

    def read_text(self, relative_path: str) -> str:
        """
        函数作用：
          读取仓库内文本文件，供静态断言复用。
        参数说明：
          relative_path：相对仓库根目录的文件路径。
        返回值说明：
          返回文件完整文本内容。
        """
        return (self.repo_root / relative_path).read_text(encoding="utf-8")

    def build_pack_tool(self, temp_dir: Path) -> Path:
        """
        函数作用：
          使用本机 gcc 编译 OTA 头部打包工具，验证工具源码可独立构建。
        参数说明：
          temp_dir：临时目录，用于保存测试生成的 exe。
        返回值说明：
          返回编译出的打包工具路径。
        """
        tool_exe = temp_dir / "pack_ota_image.exe"
        subprocess.run(
            [
                "gcc",
                "-std=c99",
                "-Wall",
                "-Wextra",
                "-Werror",
                str(self.repo_root / "tools" / "pack_ota_image.c"),
                "-o",
                str(tool_exe),
            ],
            cwd=self.repo_root,
            check=True,
        )
        return tool_exe

    def make_fake_app_bin(self) -> bytes:
        """
        函数作用：
          构造一个带合法 Cortex-M 向量表的最小 App bin，供打包工具测试使用。
        参数说明：
          无参数。
        返回值说明：
          返回原始 App bin 字节串；前 8 字节分别是 MSP 和 Reset_Handler。
        """
        vector = struct.pack("<II", 0x20001000, 0x08011101)
        payload = bytes(range(64))
        return vector + payload

    def test_pack_tool_generates_header_bin_with_crc_and_payload(self):
        """
        函数作用：
          验证 C 打包工具能把原始 Project.bin 转换成带 64 字节头部的 Project_ota.bin。
        参数说明：
          无参数。
        返回值说明：
          无返回值；断言失败时 unittest 报告具体字段不匹配。
        """
        with tempfile.TemporaryDirectory() as temp_name:
            temp_dir = Path(temp_name)
            tool_exe = self.build_pack_tool(temp_dir)
            input_bin = temp_dir / "Project.bin"
            output_bin = temp_dir / "Project_ota.bin"
            firmware = self.make_fake_app_bin()
            input_bin.write_bytes(firmware)

            result = subprocess.run(
                [
                    str(tool_exe),
                    str(input_bin),
                    str(output_bin),
                    "0x0000002A",
                    "0x08011000",
                ],
                cwd=self.repo_root,
                check=True,
                text=True,
                capture_output=True,
            )

            image = output_bin.read_bytes()
            fields = struct.unpack("<16I", image[:64])
            header_without_crc = bytearray(image[:64])
            header_without_crc[28:32] = b"\x00\x00\x00\x00"

            self.assertIn("Project_ota.bin", result.stdout)
            self.assertEqual(0x474F5441, fields[0])
            self.assertEqual(64, fields[1])
            self.assertEqual(len(firmware), fields[2])
            self.assertEqual(0x08011000, fields[3])
            self.assertEqual(0x0000002A, fields[4])
            self.assertEqual(binascii.crc32(firmware) & 0xFFFFFFFF, fields[5])
            self.assertEqual(0x20001000, fields[8])
            self.assertEqual(0x08011101, fields[9])
            self.assertEqual(binascii.crc32(header_without_crc) & 0xFFFFFFFF, fields[7])
            self.assertEqual(firmware, image[64:])

    def test_ota_app_uses_header_bin_contract_and_no_old_packet_entry(self):
        """
        函数作用：
          验证 App 侧源码已经切换到头部 bin 裸流接收，不再保留旧文件传输入口。
        参数说明：
          无参数。
        返回值说明：
          无返回值；断言失败时说明旧入口残留或新协议缺失。
        """
        uart_ota_c = self.read_text("Function/uart_ota_app.c")
        uart_ota_h = self.read_text("Function/uart_ota_app.h")
        protocol_h = self.read_text("Protocol/ota_image_protocol.h")
        system_all = self.read_text("HeaderFiles/system_all.h")
        uvproj = self.read_text("project/2026706296.uvprojx")

        self.assertIn("OTA_IMAGE_MAGIC", protocol_h)
        self.assertIn("uart_ota_feed_rx_bytes", uart_ota_c)
        self.assertIn("OTA_IMAGE_HEADER_SIZE", protocol_h)
        old_receiver_symbol = "uart_ota_" + "ym" + "odem"
        old_receiver_c = old_receiver_symbol + ".c"
        old_receiver_h = old_receiver_symbol + ".h"
        old_sender_tool = "make_uart_" + "ota_packet.py"
        old_static_test = "test_" + "ym" + "odem_ota_static.py"

        self.assertNotIn(old_receiver_symbol, uart_ota_c)
        self.assertNotIn("UART_OTA_FILE_TRANSFER_CRC_REQ", uart_ota_c)
        self.assertNotIn("UART_OTA_FRAME_START", uart_ota_c)
        self.assertNotIn(old_receiver_h, system_all)
        self.assertNotIn(old_receiver_c, uvproj)

    def test_ota_header_parse_and_crc_live_in_protocol_layer(self):
        """
        函数作用：
          验证 OTA 头部字段解析、头部 CRC 和向量表元数据校验被放入 Protocol 层。
          这样 Function 层只保留接收状态机和业务交接逻辑，符合题目对协议层
          单独放置帧解析、应答组帧和 CRC 校验的目录要求。
        参数说明：
          无参数。
        返回值说明：
          无返回值；断言失败时说明 OTA 协议职责仍残留在业务逻辑层。
        """
        protocol_h = self.read_text("Protocol/ota_image_protocol.h")
        protocol_c = self.read_text("Protocol/ota_image_protocol.c")
        uart_ota_c = self.read_text("Function/uart_ota_app.c")
        uvproj = self.read_text("project/2026706296.uvprojx")

        self.assertIn("OTA_IMAGE_MAGIC", protocol_h)
        self.assertIn("OTA_IMAGE_HEADER_SIZE", protocol_h)
        self.assertIn("ota_image_header_t", protocol_h)
        self.assertIn("ota_image_parse_header", protocol_h)
        self.assertIn("ota_image_validate_header", protocol_h)
        self.assertIn("ota_image_crc32_update", protocol_h)
        self.assertIn("ota_image_read_u32_le", protocol_c)
        self.assertIn("header_for_crc[28] = 0U", protocol_c)
        self.assertIn("OTA_IMAGE_MAGIC != header->magic", protocol_c)
        self.assertIn("ota_image_parse_header", uart_ota_c)
        self.assertIn("ota_image_validate_header", uart_ota_c)
        self.assertIn("ota_image_crc32_update", uart_ota_c)
        # Function 层允许保留 OTA 接收会话状态结构体；这里只禁止协议头结构体和解析工具回流。
        self.assertIn("} uart_ota_session_t;", uart_ota_c)
        self.assertNotIn("} ota_image_header_t;", uart_ota_c)
        self.assertNotIn("uint32_t magic;\n    uint32_t header_size;\n    uint32_t image_size;", uart_ota_c)
        self.assertNotIn("prv_uart_ota_read_u32_le", uart_ota_c)
        self.assertNotIn("prv_uart_ota_crc32_calc", uart_ota_c)
        self.assertIn("<FilePath>..\\Protocol\\ota_image_protocol.c</FilePath>", uvproj)

    def test_usart_dma_circular_handler_is_enabled_for_raw_file_stream(self):
        """
        函数作用：
          验证裸流接收路径启用了 USART1 DMA circular + HTF/FTF 提示中断。
        参数说明：
          无参数。
        返回值说明：
          无返回值；断言失败时说明连续裸流接收链路可能仍依赖停 DMA 重装载。
        """
        bsp_usart_c = self.read_text("Driver/USART/bsp_usart.c")
        irq_c = self.read_text("User/gd32f4xx_it.c")
        irq_h = self.read_text("User/gd32f4xx_it.h")

        self.assertIn("dma_circulation_enable(USART1_RX_DMA_PERIPH", bsp_usart_c)
        self.assertIn("dma_interrupt_enable(USART1_RX_DMA_PERIPH", bsp_usart_c)
        self.assertIn("DMA_INT_HTF | DMA_INT_FTF", bsp_usart_c)
        self.assertIn("DMA_INT_FLAG_HTF", irq_c)
        self.assertIn("DMA_INT_FTF", bsp_usart_c)
        self.assertIn("DMA0_Channel5_IRQHandler", irq_c)
        self.assertIn("uart_ota_rx_flag = 1U", irq_c)
        self.assertNotIn("dma_transfer_number_config(USART1_RX_DMA_PERIPH", irq_c)
        self.assertIn("uart_ota_take_ring_bytes", self.read_text("Function/uart_ota_app.c"))
        self.assertIn("void DMA0_Channel5_IRQHandler(void);", irq_h)

    def test_streaming_raw_ota_uses_circular_dma_and_no_full_payload_ram(self):
        """
        函数作用：
          验证连续裸流 OTA 改为“ready 前预擦下载区 + DMA 环形缓冲 + payload 边收边写 Flash”。
        参数说明：
          无参数。
        返回值说明：
          无返回值；断言失败时说明实现仍依赖整包 RAM 或 DMA 重装载，无法支撑无停顿裸流发送。
        """
        uart_ota_c = self.read_text("Function/uart_ota_app.c")
        uart_ota_h = self.read_text("Function/uart_ota_app.h")
        bsp_usart_c = self.read_text("Driver/USART/bsp_usart.c")
        scheduler_c = self.read_text("Function/scheduler.c")
        irq_c = self.read_text("User/gd32f4xx_it.c")
        ota_spec = self.read_text(".trellis/spec/backend/embedded-ota-guidelines.md")

        self.assertIn("#define UART_OTA_RING_BUFFER_SIZE    BSP_USART1_RX_BUFFER_SIZE", uart_ota_h)
        self.assertIn("#define BSP_USART1_RX_BUFFER_SIZE      (32U * 1024U)", self.read_text("Driver/USART/bsp_usart.h"))
        self.assertIn("usart1_rxbuffer", uart_ota_c)
        self.assertIn("uart_ota_prepare_download_area_before_ready", uart_ota_c)
        self.assertIn("uart_ota_take_ring_bytes", uart_ota_c)
        self.assertIn("prv_uart_ota_stream_flash_write", uart_ota_c)
        self.assertIn("UART_OTA_STATE_ERASING_DOWNLOAD", uart_ota_c)
        self.assertIn("dma_circulation_enable(USART1_RX_DMA_PERIPH", bsp_usart_c)
        self.assertIn("memory0_addr = (uint32_t)usart1_rxbuffer", bsp_usart_c)
        self.assertIn("sizeof(usart1_rxbuffer)", bsp_usart_c)
        self.assertNotIn("static uint8_t g_uart_ota_payload_buffer", uart_ota_c)
        self.assertNotIn("UART_OTA_PAYLOAD_BUFFER_SIZE", uart_ota_h)
        self.assertNotIn("dma_transfer_number_config(USART1_RX_DMA_PERIPH", irq_c)
        self.assertIn("uart_ota_prepare_download_area_before_ready()", scheduler_c)
        self.assertLess(
            scheduler_c.find("uart_ota_prepare_download_area_before_ready()"),
            scheduler_c.find("uart_ota_emit_startup_probe();"),
        )
        self.assertIn("USART1 DMA circular ring", ota_spec)
        self.assertIn("Pre-ready erase", ota_spec)

    def test_ota_ready_probe_is_emitted_after_scheduler_is_ready(self):
        """
        函数作用：
          验证 RS485 OTA ready 探测串只会在调度器初始化之后发出。
          这样上位机看到 ready 后立即发送 Project_ota.bin 时，uart_ota_task()
          已经具备在主循环中消费 DMA 队列的条件，避免启动自检期间队列溢出。
        参数说明：
          无参数。
        返回值说明：
          无返回值；断言失败时说明 ready 提示仍早于任务消费能力。
        """
        scheduler_c = self.read_text("Function/scheduler.c")
        scheduler_ready_index = scheduler_c.find("scheduler_init();")
        probe_index = scheduler_c.find("uart_ota_emit_startup_probe();")

        self.assertGreaterEqual(scheduler_ready_index, 0)
        self.assertGreaterEqual(probe_index, 0)
        self.assertLess(scheduler_ready_index, probe_index)

    def test_ota_error_state_can_resync_on_next_header_magic(self):
        """
        函数作用：
          验证 OTA 错误态不会永久吞掉后续数据，而是能在下一次合法头部 magic
          出现时重置会话并重新接收。
        参数说明：
          无参数。
        返回值说明：
          无返回值；断言失败时说明用户发错文件后仍必须复位才能重试。
        """
        uart_ota_c = self.read_text("Function/uart_ota_app.c")

        self.assertIn("prv_uart_ota_try_resync_from_error", uart_ota_c)
        self.assertIn("UART_OTA_STATE_ERROR == g_uart_ota_session.state", uart_ota_c)
        self.assertIn("prv_uart_ota_reset_session(1U)", uart_ota_c)
        self.assertIn("g_uart_ota_resync_magic_buffer", uart_ota_c)
        self.assertIn("g_uart_ota_resync_magic_bytes", uart_ota_c)
        self.assertIn("g_uart_ota_session.header_bytes = 4U", uart_ota_c)

    def test_keil_after_build_generates_project_ota_bin(self):
        """
        函数作用：
          验证 Keil After Build 已经从只生成 Project.bin 改为继续生成 Project_ota.bin。
        参数说明：
          无参数。
        返回值说明：
          无返回值；断言失败时说明 Keil 后处理命令未接入头部打包工具。
        """
        uvproj = self.read_text("project/2026706296.uvprojx")

        self.assertIn("fromelf.exe --bin --output=.\\output\\Project.bin", uvproj)
        self.assertIn("pack_ota_image.exe", uvproj)
        self.assertIn(".\\output\\Project_ota.bin", uvproj)
        self.assertIn("<RunUserProg2>1</RunUserProg2>", uvproj)
        self.assertIn("<nStopA2X>0</nStopA2X>", uvproj)
        self.assertNotIn("&amp;&amp;", uvproj)

    def test_removed_sender_files_are_absent(self):
        """
        函数作用：
          验证旧辅助发送工具和旧静态测试文件已经从仓库中删除。
        参数说明：
          无参数。
        返回值说明：
          无返回值；断言失败时说明旧辅助入口仍残留在源码树。
        """
        old_receiver_base = "uart_ota_" + "ym" + "odem"
        old_sender_tool = "make_uart_" + "ota_packet.py"
        old_static_test = "test_" + "ym" + "odem_ota_static.py"

        self.assertFalse((self.repo_root / "tools" / old_sender_tool).exists())
        self.assertFalse((self.repo_root / "tools" / old_static_test).exists())
        self.assertFalse((self.repo_root / "Function" / (old_receiver_base + ".c")).exists())
        self.assertFalse((self.repo_root / "Function" / (old_receiver_base + ".h")).exists())

    def test_boot_parameter_magic_matches_app_bootloader_and_docs(self):
        """
        函数作用：
          验证 App、独立 BootLoader 和 OTA 规格文档使用同一个参数区魔术字。
          这个魔术字是 App 写参数区、BootLoader 判断参数区有效性的共享协议值，
          两边只要有一处不同，在线升级就会表现为 BootLoader 放弃搬运并直接尝试跳 App。
        参数说明：
          无参数。
        返回值说明：
          无返回值；断言失败时说明跨工程共享协议常量不同步。
        """
        bootloader_root = self.repo_root.parent / "2026706296_bootloader"
        if not bootloader_root.exists():
            self.skipTest("独立 BootLoader 工程不在当前工作区旁边，跳过跨工程魔术字一致性检查。")

        app_header = self.read_text("Driver/BOOTLOADER/bootloader_port.h")
        ota_spec = self.read_text(".trellis/spec/backend/embedded-ota-guidelines.md")
        bootloader_function = (bootloader_root / "Function" / "Function.c").read_text(encoding="utf-8")
        bootloader_config_c = (bootloader_root / "Driver" / "BootLoader" / "BootConfig.c").read_text(encoding="utf-8")
        bootloader_config_h = (bootloader_root / "Driver" / "BootLoader" / "BootConfig.h").read_text(encoding="utf-8")

        self.assertIn("BOOTLOADER_PORT_MAGIC_WORD         " + self.expected_boot_magic + "UL", app_header)
        self.assertIn("BOOT_PARAM_MAGIC            (" + self.expected_boot_magic + "UL)", bootloader_function)
        self.assertIn("tmp_param.magicWord = " + self.expected_boot_magic + ";", bootloader_config_c)
        self.assertIn("魔术字:", bootloader_config_h)
        self.assertIn(self.expected_boot_magic, bootloader_config_h)
        self.assertIn("| `magicWord` | `" + self.expected_boot_magic + "` |", ota_spec)
        self.assertNotIn("0x5AA5C33C", app_header)
        self.assertNotIn("0x5AA5C33C", bootloader_function)
        self.assertNotIn("0x5AA5C33C", bootloader_config_c)
        self.assertNotIn("0x5AA5C33C", bootloader_config_h)
        self.assertNotIn("0x5AA5C33C", ota_spec)

    def test_ota_partition_is_three_128kb_regions_across_app_bootloader_and_docs(self):
        """
        函数作用：
          验证 App、独立 BootLoader、Keil IROM、打包工具和规格文档使用同一套
          运行区、备份区、缓存区各 128KB 的三分区 OTA 契约。
        参数说明：
          无参数。
        返回值说明：
          无返回值；断言失败时说明跨工程 Flash 分区或 OTA 容量描述不同步。
        """
        bootloader_root = self.repo_root.parent / "2026706296_bootloader"
        if not bootloader_root.exists():
            self.skipTest("独立 BootLoader 工程不在当前工作区旁边，跳过跨工程分区一致性检查。")

        app_header = self.read_text("Driver/BOOTLOADER/bootloader_port.h")
        app_config = self.read_text("User/boot_app_config.h")
        pack_tool = self.read_text("tools/pack_ota_image.c")
        uvproj = self.read_text("project/2026706296.uvprojx")
        ota_spec = self.read_text(".trellis/spec/backend/embedded-ota-guidelines.md")
        project_doc = self.read_text("工程文档.md")
        bootloader_function = (bootloader_root / "Function" / "Function.c").read_text(encoding="utf-8")
        bootloader_doc = (bootloader_root / "BootLoader_程序详解.md").read_text(encoding="utf-8")

        self.assertIn("BOOTLOADER_PORT_PARAM_ADDR         " + self.expected_param_addr + "UL", app_header)
        self.assertIn("BOOTLOADER_PORT_BACKUP_ADDR        " + self.expected_backup_addr + "UL", app_header)
        self.assertIn("BOOTLOADER_PORT_DOWNLOAD_ADDR      " + self.expected_download_addr + "UL", app_header)
        self.assertIn("BOOTLOADER_PORT_REGION_SIZE        (" + self.expected_region_size_kb + "U * 1024U)", app_header)
        self.assertIn("BOOTLOADER_PORT_DOWNLOAD_MAX_SIZE  BOOTLOADER_PORT_REGION_SIZE", app_header)
        self.assertIn("BOOTLOADER_PORT_APP_MAX_SIZE       " + self.expected_app_region_size + "UL", app_header)
        self.assertIn("BOOT_APP_FLASH_SIZE             (" + self.expected_app_region_size + "UL)", app_config)
        self.assertIn("OTA_IMAGE_MAX_SIZE           (" + self.expected_region_size_kb + "UL * 1024UL)", pack_tool)
        self.assertIn("OTA_APP_REGION_SIZE          " + self.expected_app_region_size + "UL", pack_tool)
        self.assertIn("IROM(0x08011000,0x020000)", uvproj)
        self.assertIn("..\\Protocol", uvproj)
        self.assertIn("<GroupName>Driver</GroupName>", uvproj)
        self.assertIn("<GroupName>Protocol</GroupName>", uvproj)
        self.assertNotIn("..\\HardWare\\", uvproj)
        self.assertIn("BOOT_APP_START_ADDR         (" + self.expected_app_addr + "UL)", bootloader_function)
        self.assertIn("BOOT_PARAM_ADDR             (" + self.expected_param_addr + "UL)", bootloader_function)
        self.assertIn("APP_BACKUP_ADDR             (" + self.expected_backup_addr + "UL)", bootloader_function)
        self.assertIn("APP_DOWNLOAD_ADDR           (" + self.expected_download_addr + "UL)", bootloader_function)
        self.assertIn("BOOT_APP_REGION_SIZE        (" + self.expected_app_region_size + "UL)", bootloader_function)
        self.assertIn("APP_DOWNLOAD_MAX_SIZE       BOOT_APP_REGION_SIZE", bootloader_function)
        self.assertIn("Backup_Transport", bootloader_function)
        self.assertIn("Restore_Backup_To_App", bootloader_function)
        self.assertIn("128KB", ota_spec)
        self.assertIn("0x08010000", ota_spec)
        self.assertIn("0x08011000", ota_spec)
        self.assertIn("0x08031000", ota_spec)
        self.assertIn("0x08051000", ota_spec)
        self.assertIn("0x08070FFF", ota_spec)
        self.assertIn("128KB", project_doc)
        self.assertIn("0x08010000", project_doc)
        self.assertIn("0x08011000", project_doc)
        self.assertIn("0x08031000", project_doc)
        self.assertIn("0x08051000", project_doc)
        self.assertIn("128KB", bootloader_doc)
        self.assertIn("0x08010000", bootloader_doc)
        self.assertIn("0x08011000", bootloader_doc)
        self.assertIn("0x08031000", bootloader_doc)
        self.assertIn("0x08051000", bootloader_doc)
        self.assertIn("Project_ota.bin", ota_spec)
        self.assertIn("64", ota_spec)


if __name__ == "__main__":
    unittest.main()
