# UART OTA Streaming Implementation Plan

> **For Codex:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Reduce App RAM usage by replacing the current full-file USART0 OTA buffer with a packetized streaming OTA flow that writes each firmware chunk directly into the existing internal Flash download buffer.

**Architecture:** Keep the current BootLoader handoff unchanged: App still writes firmware to `0x08073000`, writes BootLoader parameters at `0x0800C000`, and resets into BootLoader. Replace only the App-side transport: PC sends a start frame, many data frames, and an end frame; App validates sequence, CRC, vector table, and Flash writeback before setting update flags. The first implementation keeps the current `52KB` internal download-buffer limit; external SPI Flash support can be a second phase.

**Tech Stack:** GD32F470 bare-metal C with GD32 standard peripheral library, USART0 IDLE + DMA, internal FMC Flash writes, Python 3 sender/generator tooling, Keil UV4 build.

---

## Design Summary

### Protocol

Use a framed binary protocol over USART0. All multi-byte fields are little-endian.

| Frame | Direction | Payload |
|-------|-----------|---------|
| `START` | PC to App | `magic`, `type=1`, `version`, `firmware_size`, `firmware_crc32`, `header_crc32` |
| `START_ACK` | App to PC | status byte and accepted `chunk_size` |
| `DATA` | PC to App | `magic`, `type=2`, `seq`, `offset`, `length`, `chunk_crc32`, chunk bytes |
| `DATA_ACK` | App to PC | status byte, `seq`, `next_offset` |
| `END` | PC to App | `magic`, `type=3`, `firmware_size`, `firmware_crc32` |
| `END_ACK` | App to PC | status byte; success means App will reset into BootLoader |

The PC tool waits for ACK after each frame. This avoids relying on one giant DMA receive buffer and keeps USART0 receive buffers small.

### RAM Goal

| Buffer | Current | Target |
|--------|---------|--------|
| `usart0_rxbuffer` | `52KB + 16B` | `1024B` or `2048B` |
| `uart_dma_buffer` | `52KB + 16B` | same as USART0 small frame buffer |
| OTA staging | full firmware in RAM | one chunk, copied from received frame |

Expected static RAM reduction is about `100KB`.

---

## Task 1: Add Python Protocol Unit Tests

**Files:**
- Create: `tools/test_uart_ota_packet.py`
- Modify: `tools/make_uart_ota_packet.py`

**Step 1: Write failing tests**

Create tests for:

- `build_start_frame()` encodes little-endian fields and CRC.
- `build_data_frame()` preserves `seq`, `offset`, `length`, and `chunk_crc32`.
- `iter_chunks()` splits firmware into exact chunk boundaries.

Use only the Python standard library so the project does not need new dependencies:

```powershell
python -m unittest tools.test_uart_ota_packet
```

Expected before implementation: tests fail because the streaming protocol helpers do not exist.

**Step 2: Implement minimal protocol helpers**

In `tools/make_uart_ota_packet.py`, add:

- constants for frame magic and frame types
- `crc32_bytes(data: bytes) -> int`
- `build_start_frame(version: int, firmware: bytes) -> bytes`
- `build_data_frame(seq: int, offset: int, chunk: bytes) -> bytes`
- `build_end_frame(firmware: bytes) -> bytes`
- `iter_chunks(firmware: bytes, chunk_size: int) -> Iterator[tuple[int, bytes]]`

Keep existing `.uota` full-file generation working during this task.

**Step 3: Verify tests pass**

Run:

```powershell
python -m unittest tools.test_uart_ota_packet
```

Expected: all tests pass.

**Step 4: Commit**

```powershell
git add tools/make_uart_ota_packet.py tools/test_uart_ota_packet.py
git commit -m "test(ota): 覆盖串口分包协议生成"
```

---

## Task 2: Add Streaming Sender Mode

**Files:**
- Modify: `tools/make_uart_ota_packet.py`

**Step 1: Write failing CLI tests**

Extend `tools/test_uart_ota_packet.py` to verify:

- `--stream-info` prints firmware size, CRC, and chunk count.
- invalid `--chunk-size` is rejected.

Expected before implementation: tests fail because CLI mode is missing.

**Step 2: Implement sender-friendly CLI**

Add CLI options:

- `--mode packet` keeps current behavior and writes `Project.uota`.
- `--mode stream-info` prints frame/chunk metadata without opening a serial port.
- `--mode send --port COMx` opens a serial port, sends START/DATA/END frames, and waits for ACK after every frame.
- `--chunk-size <N>` defaults to `512`, must fit in App USART0 frame buffer.

`--mode send` imports `pyserial` only when that mode is used. If the module is missing, the tool prints the install hint instead of affecting packet generation or `stream-info`.

**Step 3: Verify**

Run:

```powershell
python -m unittest tools.test_uart_ota_packet
python tools\make_uart_ota_packet.py --mode stream-info --version 0x00000003 --chunk-size 512
```

Expected: tests pass and CLI prints size/CRC/chunk count.

**Step 4: Commit**

```powershell
git add tools/make_uart_ota_packet.py tools/test_uart_ota_packet.py
git commit -m "feat(ota): 增加分包协议工具模式"
```

---

## Task 3: Implement App Streaming OTA State Machine

**Files:**
- Modify: `USER/App/usart_app.c`
- Modify: `USER/App/usart_app.h`
- Modify: `USER/gd32f4xx_it.c`

**Step 1: Add parser state and frame handlers**

In `USER/App/usart_app.c`, replace the full-file packet parser with a streaming state machine:

- idle state accepts `START`.
- active state accepts monotonically increasing `DATA.seq` and exact `offset`.
- end state accepts `END`, verifies total bytes and CRC, writes BootLoader parameter area, then resets.

Keep these private helpers:

- little-endian read helper
- CRC32 helper
- vector-table validator
- internal Flash erase/write/CRC helpers
- BootLoader parameter writer

**Step 2: Write Flash incrementally**

On `START`:

- validate `firmware_size` is `1..52KB`
- erase only the required pages at `0x08073000`
- reset running CRC state and offset counters

On each `DATA`:

- reject wrong sequence or offset
- reject chunk length larger than configured max
- verify chunk CRC
- if offset is `0`, validate firmware vector table from first chunk
- write chunk directly to `0x08073000 + offset`
- update running CRC and counters

On `END`:

- compare running CRC against `firmware_crc32`
- optionally read back Flash CRC from `0x08073000`
- write BootLoader parameters and reset

**Step 3: Keep normal UART forwarding**

If the frame magic does not match the OTA streaming magic, keep the existing USART0-to-RS485 transparent forwarding behavior.

**Step 4: Verify build**

Run:

```powershell
& 'E:\Keil_v5\UV4\UV4.exe' -b 'MDK\2026706296.uvprojx' -j0
Select-String -Path 'MDK\output\Project.build_log.htm' -Pattern 'Error\(s\)|Warning\(s\)'
```

Expected: `0 Error(s)`.

**Step 5: Commit**

```powershell
git add USER/App/usart_app.c USER/App/usart_app.h USER/gd32f4xx_it.c
git commit -m "feat(ota): App侧支持USART0分包写入"
```

---

## Task 4: Reduce USART0 Buffers

**Files:**
- Modify: `USER/Driver/bsp_usart.h`
- Modify: `USER/App/usart_app.h`

**Step 1: Lower buffer constants**

Set:

```c
#define BSP_USART0_RX_BUFFER_SIZE      1024U
#define UART_APP_DMA_BUFFER_SIZE       BSP_USART0_RX_BUFFER_SIZE
```

If frame overhead plus default chunk size exceeds `1024`, use `2048`.

**Step 2: Verify RAM usage**

Run:

```powershell
& 'E:\Keil_v5\UV4\UV4.exe' -b 'MDK\2026706296.uvprojx' -j0
& 'E:\Keil_v5\ARM\ARMCLANG\bin\fromelf.exe' --text -z 'MDK\output\Project.axf'
```

Expected: `ZI Data` drops significantly from the previous `121968` bytes.

**Step 3: Commit**

```powershell
git add USER/Driver/bsp_usart.h USER/App/usart_app.h
git commit -m "perf(ota): 降低USART0升级缓冲RAM占用"
```

---

## Task 5: Update Specs and User Docs

**Files:**
- Modify: `.trellis/spec/backend/embedded-ota-guidelines.md`
- Modify: `BootLoader_APP_接入说明.md`
- Modify: `工程文档.md`

**Step 1: Update contracts**

Document:

- streaming protocol frames
- small buffer requirement
- retained `52KB` internal download-buffer limit
- verification commands
- hardware serial test logs

**Step 2: Verify docs mention the new flow**

Run:

```powershell
rg -n "stream|分包|chunk|52KB|Project.uota|stream-info" .trellis\spec\backend\embedded-ota-guidelines.md BootLoader_APP_接入说明.md 工程文档.md
```

Expected: all three docs describe the streaming flow and no longer state that USART0 RAM must hold the complete `.uota` file.

**Step 3: Commit**

```powershell
git add .trellis/spec/backend/embedded-ota-guidelines.md BootLoader_APP_接入说明.md 工程文档.md
git commit -m "docs(ota): 更新分包升级流程说明"
```

---

## Task 6: Final Verification and Push

**Files:**
- No code edits unless verification finds a defect.

**Step 1: Run full verification**

```powershell
python -m unittest tools.test_uart_ota_packet
python tools\make_uart_ota_packet.py --mode stream-info --version 0x00000003 --chunk-size 512
& 'E:\Keil_v5\UV4\UV4.exe' -b 'MDK\2026706296.uvprojx' -j0
Select-String -Path 'MDK\output\Project.build_log.htm' -Pattern 'Error\(s\)|Warning\(s\)'
& 'E:\Keil_v5\ARM\ARMCLANG\bin\fromelf.exe' --text -z 'MDK\output\Project.axf'
git status -sb
```

Expected:

- Python tests pass.
- Keil build reports `0 Error(s)`.
- `ZI Data` is substantially below the previous `121968` bytes.
- Only expected files are modified.

**Step 2: Push**

```powershell
git push origin main
```

---

## Final Implementation Notes

- Do not rewrite BootLoader to read external SPI Flash in this phase.
- Do not move App start address or BootLoader parameter address.
- Do not remove the current internal `52KB` download-buffer limit yet.
- The implementation did add an optional `--mode send` path. It depends on `pyserial` only at runtime for real serial sending; unit tests and packet generation still use the Python standard library.
