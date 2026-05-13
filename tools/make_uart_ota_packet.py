#!/usr/bin/env python3
"""
脚本作用：
  生成 App 侧串口升级协议使用的发送文件。
主要流程：
  1. 读取 Keil/fromelf 生成的 Project.bin。
  2. 在文件开头追加小端 magic、appVersion、固件大小和 CRC32。
  3. 输出 16 字节包头 + 原始 bin 的 ota 文件，供串口助手“发送文件”使用。
参数说明：
  第 1 个位置参数：输入 bin 文件路径，默认 MDK/output/Project.bin。
  第 2 个位置参数：输出 ota 文件路径，默认 MDK/output/Project.uota。
  --version：写入包头的 App 版本号，支持十进制或 0x 前缀十六进制，默认 0x00000001。
返回值说明：
  0：生成成功。
  非 0：输入文件不存在、读取失败或写入失败。
"""

from __future__ import annotations

import argparse
import binascii
import struct
from pathlib import Path


UART_OTA_MAGIC = 0x5AA5C33C


def parse_u32(value: str) -> int:
    """
    函数作用：
      将命令行传入的版本号解析为 32 位无符号整数。
    参数说明：
      value：版本号字符串，可为十进制，也可为 0x 前缀十六进制。
    返回值说明：
      返回 0~0xFFFFFFFF 范围内的整数；超出范围时抛出 argparse.ArgumentTypeError。
    """
    number = int(value, 0)
    if number < 0 or number > 0xFFFFFFFF:
        raise argparse.ArgumentTypeError("version must fit in uint32")
    return number


def build_packet(input_bin: Path, output_file: Path, app_version: int) -> tuple[int, int, int]:
    """
    函数作用：
      根据输入 bin 生成串口升级发送文件。
    主要流程：
      1. 读取原始 Project.bin 内容。
      2. 计算固件 CRC32，并使用 struct.pack('<IIII') 生成小端包头。
      3. 创建输出目录并写入完整升级包。
    参数说明：
      input_bin：原始 Project.bin 路径。
      output_file：生成的升级包路径。
      app_version：写入包头的 App 版本号。
    返回值说明：
      返回三元组：(生成文件总字节数, 固件原始长度, 固件 CRC32)。
    """
    firmware = input_bin.read_bytes()
    firmware_crc32 = binascii.crc32(firmware) & 0xFFFFFFFF
    packet = struct.pack("<IIII", UART_OTA_MAGIC, app_version, len(firmware), firmware_crc32) + firmware
    output_file.parent.mkdir(parents=True, exist_ok=True)
    output_file.write_bytes(packet)
    return len(packet), len(firmware), firmware_crc32


def main() -> int:
    """
    函数作用：
      命令行入口，解析参数并生成串口升级包。
    参数说明：
      无显式参数，直接读取 sys.argv。
    返回值说明：
      0：生成成功。
    """
    parser = argparse.ArgumentParser(description="Generate UART OTA packet for this GD32 BootLoader App.")
    parser.add_argument("input_bin", nargs="?", default="MDK/output/Project.bin")
    parser.add_argument("output_file", nargs="?", default="MDK/output/Project.uota")
    parser.add_argument("--version", type=parse_u32, default=0x00000001)
    args = parser.parse_args()

    input_bin = Path(args.input_bin)
    output_file = Path(args.output_file)
    total_size, firmware_size, firmware_crc32 = build_packet(input_bin, output_file, args.version)
    print(
        f"generated {output_file} ({total_size} bytes), "
        f"firmware={firmware_size} bytes, "
        f"crc=0x{firmware_crc32:08X}, "
        f"version=0x{args.version:08X}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
