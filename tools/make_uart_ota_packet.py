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
import time
from pathlib import Path
from typing import Iterator, Protocol, Sequence


UART_OTA_MAGIC = 0x5AA5C33C
UART_OTA_STREAM_MAGIC = 0xA55A5AA5
UART_OTA_FRAME_START = 1
UART_OTA_FRAME_DATA = 2
UART_OTA_FRAME_END = 3
UART_OTA_FRAME_ACK_BASE = 0x80
UART_OTA_ACK_FRAME_SIZE = 20


class StreamTransport(Protocol):
    """
    协议作用：
      描述流式 OTA 发送函数需要的最小串口传输接口。
    方法说明：
      write(data)：写入一帧二进制数据。
      read(size)：读取最多 size 字节应答数据。
    """

    def write(self, data: bytes) -> object:
        ...

    def read(self, size: int) -> bytes:
        ...


def crc32_bytes(data: bytes) -> int:
    """
    函数作用：
      计算协议中使用的 CRC32 值，并统一裁剪为 32 位无符号整数。
    参数说明：
      data：参与 CRC32 计算的原始字节串。
    返回值说明：
      返回 0~0xFFFFFFFF 范围内的 CRC32 结果。
    """
    return binascii.crc32(data) & 0xFFFFFFFF


def build_start_frame(app_version: int, firmware: bytes) -> bytes:
    """
    函数作用：
      生成分包 OTA 的 START 帧。
    主要流程：
      1. 计算完整固件 CRC32。
      2. 按小端格式打包 magic、帧类型、版本、固件长度和固件 CRC。
      3. 对前 20 字节头部再计算 header_crc32，便于 App 侧快速拒绝损坏头。
    参数说明：
      app_version：写入升级参数区的 App 版本号。
      firmware：完整 Project.bin 内容。
    返回值说明：
      返回 START 帧字节串，长度固定为 24 字节。
    """
    header_without_crc = struct.pack(
        "<IIIII",
        UART_OTA_STREAM_MAGIC,
        UART_OTA_FRAME_START,
        app_version,
        len(firmware),
        crc32_bytes(firmware),
    )
    return header_without_crc + struct.pack("<I", crc32_bytes(header_without_crc))


def build_data_frame(seq: int, offset: int, chunk: bytes) -> bytes:
    """
    函数作用：
      生成分包 OTA 的 DATA 帧。
    主要流程：
      1. 计算当前分包数据的 CRC32。
      2. 按小端格式写入序号、Flash 偏移、分包长度和分包 CRC。
      3. 将原始分包数据追加在帧头之后。
    参数说明：
      seq：分包序号，从 0 开始递增。
      offset：该分包在完整固件中的字节偏移。
      chunk：当前分包原始数据。
    返回值说明：
      返回 DATA 帧字节串，格式为 24 字节头 + chunk。
    """
    header = struct.pack(
        "<IIIIII",
        UART_OTA_STREAM_MAGIC,
        UART_OTA_FRAME_DATA,
        seq,
        offset,
        len(chunk),
        crc32_bytes(chunk),
    )
    return header + chunk


def build_end_frame(firmware: bytes) -> bytes:
    """
    函数作用：
      生成分包 OTA 的 END 帧。
    参数说明：
      firmware：完整 Project.bin 内容，用于写入最终长度和 CRC。
    返回值说明：
      返回 END 帧字节串，长度固定为 16 字节。
    """
    return struct.pack(
        "<IIII",
        UART_OTA_STREAM_MAGIC,
        UART_OTA_FRAME_END,
        len(firmware),
        crc32_bytes(firmware),
    )


def build_ack_frame(frame_type: int, status: int, value0: int, value1: int) -> bytes:
    """
    函数作用：
      生成 App 侧 ACK 帧的字节串，主要供单元测试和 ACK 解析自检使用。
    参数说明：
      frame_type：被应答的帧类型，例如 UART_OTA_FRAME_DATA。
      status：App 侧处理状态，0 表示成功。
      value0：ACK 附带值 0。
      value1：ACK 附带值 1。
    返回值说明：
      返回 20 字节 ACK 帧。
    """
    return struct.pack(
        "<IIIII",
        UART_OTA_STREAM_MAGIC,
        UART_OTA_FRAME_ACK_BASE | frame_type,
        status,
        value0,
        value1,
    )


def find_ack_frame(data: bytes, frame_type: int) -> tuple[int, int, int] | None:
    """
    函数作用：
      从串口返回数据中查找指定帧类型对应的 ACK。
    主要流程：
      1. 扫描二进制魔术字位置，允许前面混有 App 日志文本。
      2. 找到 20 字节 ACK 后解析 magic、ack_type、status、value0、value1。
      3. 只返回与当前发送帧类型匹配的 ACK。
    参数说明：
      data：串口接收到的累计字节。
      frame_type：当前等待应答的原始帧类型。
    返回值说明：
      找到匹配 ACK 时返回 `(status, value0, value1)`；否则返回 None。
    """
    magic = struct.pack("<I", UART_OTA_STREAM_MAGIC)
    expected_ack_type = UART_OTA_FRAME_ACK_BASE | frame_type
    start = 0
    while True:
        index = data.find(magic, start)
        if index < 0:
            return None
        if index + UART_OTA_ACK_FRAME_SIZE > len(data):
            return None
        ack_magic, ack_type, status, value0, value1 = struct.unpack(
            "<IIIII",
            data[index:index + UART_OTA_ACK_FRAME_SIZE],
        )
        if ack_magic == UART_OTA_STREAM_MAGIC and ack_type == expected_ack_type:
            return status, value0, value1
        start = index + 1


def iter_chunks(firmware: bytes, chunk_size: int) -> Iterator[tuple[int, bytes]]:
    """
    函数作用：
      按固定大小把固件拆成带偏移的分包。
    参数说明：
      firmware：完整 Project.bin 内容。
      chunk_size：每个分包的最大数据长度，必须大于 0。
    返回值说明：
      逐个返回 `(offset, chunk)`；offset 为 chunk 在完整固件中的起始偏移。
    """
    if chunk_size <= 0:
        raise ValueError("chunk_size must be greater than 0")

    for offset in range(0, len(firmware), chunk_size):
        yield offset, firmware[offset:offset + chunk_size]


def wait_for_ack(transport: StreamTransport, frame_type: int, ack_timeout: float) -> tuple[int, int, int]:
    """
    函数作用：
      等待 App 对当前帧返回 ACK。
    参数说明：
      transport：串口传输对象，必须提供 read(size)。
      frame_type：当前等待 ACK 的帧类型。
      ack_timeout：超时时间，单位秒。
    返回值说明：
      返回 `(status, value0, value1)`；超时或非 0 状态会抛出 RuntimeError。
    """
    deadline = time.monotonic() + ack_timeout
    rx = bytearray()
    while time.monotonic() < deadline:
        chunk = transport.read(64)
        if chunk:
            rx.extend(chunk)
            ack = find_ack_frame(bytes(rx), frame_type)
            if ack is not None:
                status, value0, value1 = ack
                if status != 0:
                    raise RuntimeError(
                        f"device rejected frame type {frame_type}, "
                        f"status={status}, value0={value0}, value1={value1}"
                    )
                return ack
        else:
            time.sleep(0.005)

    raise RuntimeError(f"timeout waiting ack for frame type {frame_type}")


def send_stream(
    transport: StreamTransport,
    firmware: bytes,
    app_version: int,
    chunk_size: int,
    ack_timeout: float = 2.0,
) -> int:
    """
    函数作用：
      通过带 ACK 的流式 OTA 协议发送完整固件。
    主要流程：
      1. 发送 START 帧并等待 ACK。
      2. 按 chunk_size 拆分固件，逐帧发送 DATA 并等待 ACK。
      3. 发送 END 帧并等待 ACK。
    参数说明：
      transport：串口传输对象，必须提供 write(data) 和 read(size)。
      firmware：完整 Project.bin 内容。
      app_version：本次升级版本号。
      chunk_size：每个 DATA 帧的固件数据长度。
      ack_timeout：每帧等待 ACK 的超时时间，单位秒。
    返回值说明：
      返回已发送帧数量，包含 START、DATA 和 END。
    """
    frame_count = 0

    start_frame = build_start_frame(app_version, firmware)
    transport.write(start_frame)
    wait_for_ack(transport, UART_OTA_FRAME_START, ack_timeout)
    frame_count += 1

    for seq, (offset, chunk) in enumerate(iter_chunks(firmware, chunk_size)):
        data_frame = build_data_frame(seq, offset, chunk)
        transport.write(data_frame)
        wait_for_ack(transport, UART_OTA_FRAME_DATA, ack_timeout)
        frame_count += 1

    end_frame = build_end_frame(firmware)
    transport.write(end_frame)
    wait_for_ack(transport, UART_OTA_FRAME_END, ack_timeout)
    frame_count += 1

    return frame_count


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


def parse_positive_u32(value: str) -> int:
    """
    函数作用：
      将命令行传入的正整数解析为 32 位无符号范围内的非零值。
    参数说明：
      value：数字字符串，可为十进制，也可为 0x 前缀十六进制。
    返回值说明：
      返回 1~0xFFFFFFFF 范围内的整数；非法时抛出 argparse.ArgumentTypeError。
    """
    number = parse_u32(value)
    if number == 0:
        raise argparse.ArgumentTypeError("value must be greater than 0")
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
    firmware_crc32 = crc32_bytes(firmware)
    packet = struct.pack("<IIII", UART_OTA_MAGIC, app_version, len(firmware), firmware_crc32) + firmware
    output_file.parent.mkdir(parents=True, exist_ok=True)
    output_file.write_bytes(packet)
    return len(packet), len(firmware), firmware_crc32


def print_stream_info(input_bin: Path, app_version: int, chunk_size: int) -> None:
    """
    函数作用：
      打印分包 OTA 传输前需要核对的固件元数据。
    参数说明：
      input_bin：原始 Project.bin 路径。
      app_version：本次升级使用的 App 版本号。
      chunk_size：计划发送的数据分包大小。
    返回值说明：
      无返回值；信息直接输出到标准输出。
    """
    firmware = input_bin.read_bytes()
    firmware_crc32 = crc32_bytes(firmware)
    chunk_count = sum(1 for _ in iter_chunks(firmware, chunk_size))
    print(
        f"stream {input_bin}: "
        f"firmware={len(firmware)} bytes, "
        f"crc=0x{firmware_crc32:08X}, "
        f"version=0x{app_version:08X}, "
        f"chunk_size={chunk_size}, "
        f"chunks={chunk_count}"
    )


def send_stream_over_serial(port: str,
                            baudrate: int,
                            input_bin: Path,
                            app_version: int,
                            chunk_size: int,
                            ack_timeout: float) -> int:
    """
    函数作用：
      打开真实串口并执行流式 OTA 发送。
    参数说明：
      port：串口号，例如 COM7。
      baudrate：串口波特率，当前 App 默认为 115200。
      input_bin：原始 Project.bin 路径。
      app_version：本次升级版本号。
      chunk_size：每个 DATA 帧的数据长度。
      ack_timeout：每帧 ACK 等待超时时间，单位秒。
    返回值说明：
      返回已发送帧数量；未安装 pyserial 或发送失败时抛出异常。
    """
    try:
        import serial  # type: ignore
    except ImportError as exc:
        raise RuntimeError("send mode requires pyserial: python -m pip install pyserial") from exc

    firmware = input_bin.read_bytes()
    with serial.Serial(port=port, baudrate=baudrate, timeout=0.02, write_timeout=ack_timeout) as ser:
        frame_count = send_stream(ser, firmware, app_version, chunk_size, ack_timeout)
    return frame_count


def main(argv: Sequence[str] | None = None) -> int:
    """
    函数作用：
      命令行入口，解析参数并生成串口升级包。
    参数说明：
      argv：可选命令行参数序列；为 None 时由 argparse 读取 sys.argv。
    返回值说明：
      0：生成成功。
    """
    parser = argparse.ArgumentParser(description="Generate or send UART OTA packets for this GD32 BootLoader App.")
    parser.add_argument("--mode", choices=("packet", "stream-info", "send"), default="packet")
    parser.add_argument("--chunk-size", type=parse_positive_u32, default=512)
    parser.add_argument("--port", default=None)
    parser.add_argument("--baudrate", type=parse_positive_u32, default=115200)
    parser.add_argument("--ack-timeout", type=float, default=2.0)
    parser.add_argument("input_bin", nargs="?", default="MDK/output/Project.bin")
    parser.add_argument("output_file", nargs="?", default="MDK/output/Project.uota")
    parser.add_argument("--version", type=parse_u32, default=0x00000001)
    args = parser.parse_args(argv)

    input_bin = Path(args.input_bin)
    output_file = Path(args.output_file)
    if args.mode == "stream-info":
        print_stream_info(input_bin, args.version, args.chunk_size)
        return 0

    if args.mode == "send":
        if not args.port:
            parser.error("--mode send requires --port COMx")
        frame_count = send_stream_over_serial(args.port,
                                              args.baudrate,
                                              input_bin,
                                              args.version,
                                              args.chunk_size,
                                              args.ack_timeout)
        print(f"sent stream frames={frame_count}, port={args.port}, baudrate={args.baudrate}")
        return 0

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
