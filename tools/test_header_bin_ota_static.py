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
        vector = struct.pack("<II", 0x20001000, 0x0800D101)
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
                    "0x0800D000",
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
            self.assertEqual(0x0800D000, fields[3])
            self.assertEqual(0x0000002A, fields[4])
            self.assertEqual(binascii.crc32(firmware) & 0xFFFFFFFF, fields[5])
            self.assertEqual(0x20001000, fields[8])
            self.assertEqual(0x0800D101, fields[9])
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
        system_all = self.read_text("HeaderFiles/system_all.h")
        uvproj = self.read_text("project/2026706296.uvprojx")

        self.assertIn("UART_OTA_IMAGE_MAGIC", uart_ota_c)
        self.assertIn("uart_ota_feed_rx_bytes", uart_ota_c)
        self.assertIn("UART_OTA_IMAGE_HEADER_SIZE", uart_ota_h)
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

    def test_usart_dma_full_handler_is_enabled_for_raw_file_stream(self):
        """
        函数作用：
          验证裸流接收路径启用了 USART1 DMA 满缓冲中断，避免连续发送大文件时等待 IDLE。
        参数说明：
          无参数。
        返回值说明：
          无返回值；断言失败时说明 DMA 满缓冲链路缺失。
        """
        bsp_usart_c = self.read_text("HardWare/USART/bsp_usart.c")
        irq_c = self.read_text("User/gd32f4xx_it.c")
        irq_h = self.read_text("User/gd32f4xx_it.h")

        self.assertIn("dma_interrupt_enable(USART1_RX_DMA_PERIPH", bsp_usart_c)
        self.assertIn("DMA_INT_FTF", bsp_usart_c)
        self.assertIn("DMA0_Channel5_IRQHandler", irq_c)
        self.assertIn("UART_OTA_RX_QUEUE_DEPTH", irq_c)
        self.assertIn("uart_ota_queue_count++", irq_c)
        self.assertIn("uart_ota_queue_count--", self.read_text("Function/uart_ota_app.c"))
        self.assertIn("uart_ota_feed_rx_bytes", self.read_text("Function/uart_ota_app.c"))
        self.assertIn("void DMA0_Channel5_IRQHandler(void);", irq_h)

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


if __name__ == "__main__":
    unittest.main()
