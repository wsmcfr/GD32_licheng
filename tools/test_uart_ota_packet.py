import binascii
import struct
import unittest

from tools import make_uart_ota_packet as ota


class UartOtaStreamingProtocolTest(unittest.TestCase):
    """
    测试作用：
      覆盖 USART0 分包 OTA 协议的 PC 侧帧生成逻辑。
    说明：
      这些测试只依赖 Python 标准库，便于在没有硬件和串口环境时先验证
      上位机生成的二进制帧是否与 App 侧解析契约一致。
    """

    def test_build_start_frame_uses_little_endian_header(self):
        """
        函数作用：
          验证 START 帧使用小端字段，并携带固件大小、版本和 CRC32。
        参数说明：
          无参数。
        返回值说明：
          无返回值；断言失败时 unittest 会报告具体字段不匹配。
        """
        firmware = bytes(range(16))
        frame = ota.build_start_frame(0x00000023, firmware)

        magic, frame_type, version, size, firmware_crc, header_crc = struct.unpack("<IIIIII", frame)

        self.assertEqual(ota.UART_OTA_STREAM_MAGIC, magic)
        self.assertEqual(ota.UART_OTA_FRAME_START, frame_type)
        self.assertEqual(0x00000023, version)
        self.assertEqual(len(firmware), size)
        self.assertEqual(binascii.crc32(firmware) & 0xFFFFFFFF, firmware_crc)
        self.assertEqual(
            binascii.crc32(frame[:20]) & 0xFFFFFFFF,
            header_crc,
        )

    def test_build_data_frame_includes_sequence_offset_length_and_chunk_crc(self):
        """
        函数作用：
          验证 DATA 帧能准确描述当前分包的序号、偏移、长度和分包 CRC。
        参数说明：
          无参数。
        返回值说明：
          无返回值；断言失败时 unittest 会报告具体字段不匹配。
        """
        chunk = b"abcdef"
        frame = ota.build_data_frame(seq=3, offset=1024, chunk=chunk)
        header = frame[:24]
        payload = frame[24:]
        magic, frame_type, seq, offset, length, chunk_crc = struct.unpack("<IIIIII", header)

        self.assertEqual(ota.UART_OTA_STREAM_MAGIC, magic)
        self.assertEqual(ota.UART_OTA_FRAME_DATA, frame_type)
        self.assertEqual(3, seq)
        self.assertEqual(1024, offset)
        self.assertEqual(len(chunk), length)
        self.assertEqual(binascii.crc32(chunk) & 0xFFFFFFFF, chunk_crc)
        self.assertEqual(chunk, payload)

    def test_iter_chunks_splits_firmware_with_offsets(self):
        """
        函数作用：
          验证固件分包迭代器按固定块大小输出偏移和数据。
        参数说明：
          无参数。
        返回值说明：
          无返回值；断言失败时 unittest 会报告切块结果不匹配。
        """
        firmware = b"0123456789"

        chunks = list(ota.iter_chunks(firmware, 4))

        self.assertEqual(
            [(0, b"0123"), (4, b"4567"), (8, b"89")],
            chunks,
        )

    def test_iter_chunks_rejects_zero_chunk_size(self):
        """
        函数作用：
          验证分包大小为 0 时会被拒绝，避免调用者进入无限循环。
        参数说明：
          无参数。
        返回值说明：
          无返回值；断言失败时 unittest 会报告未抛出期望异常。
        """
        with self.assertRaises(ValueError):
            list(ota.iter_chunks(b"abc", 0))


if __name__ == "__main__":
    unittest.main()
