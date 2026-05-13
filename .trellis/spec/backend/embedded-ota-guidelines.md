# Embedded OTA Guidelines

> Scope: USART2 App-side streaming OTA flow for the GD32F470 BootLoader_Two_Stage integration.

---

## Scenario: USART2 App-Side Streaming OTA

### 1. Scope / Trigger

Use this guideline whenever changing:

| Area | Files / Entries |
|------|-----------------|
| App OTA parser | `USER/App/uart_ota_app.c`, `USER/App/uart_ota_app.h` |
| Boot handoff helper | `USER/Driver/bootloader_port.c`, `USER/Driver/bootloader_port.h` |
| USART2 DMA handoff | `USER/gd32f4xx_it.c`, `USER/Driver/bsp_usart.h`, `USER/Driver/bsp_usart.c` |
| PC OTA tool | `tools/make_uart_ota_packet.py`, `tools/test_uart_ota_packet.py` |
| Boot handoff | BootLoader/App Flash partition constants, parameter layout, or CRC logic |

This is a cross-layer contract. The PC-side sender, App-side receiver, internal Flash layout, and BootLoader parameter reader must agree exactly.

### 2. Signatures

| Boundary | Signature / Entry | Contract |
|----------|-------------------|----------|
| Stream info | `python tools\make_uart_ota_packet.py --mode stream-info --version <u32> --chunk-size 512` | Reads `MDK/output/Project.bin`, prints size, CRC32, version, chunk size, and chunk count |
| Stream sender | `python tools\make_uart_ota_packet.py --mode send --port COMx --version <u32> --chunk-size 512` | Sends START/DATA/END frames at default `460800` baud, prints ACK progress, and waits for ACK after every frame |
| Legacy packet | `python tools\make_uart_ota_packet.py --mode packet --version <u32>` | Still writes `MDK/output/Project.uota` for offline inspection only; current low-RAM USART2 OTA must not send it directly |
| App parser | `prv_uart_ota_try_process_packet(const uint8_t *packet, uint32_t packet_length)` | Consumes one streaming frame; only returns success after download-buffer CRC and parameter writes pass |
| ISR handoff | `USART2_IRQHandler(void)` | Copies one IDLE DMA frame into `uart_ota_dma_buffer`, records `uart_ota_dma_length`, and sets `uart_ota_rx_flag` |
| Task polling | `uart_ota_task(void)` | Handles OTA frames on USART2; normal USART0-to-RS485 forwarding stays in `uart_task(void)` |
| Wiring probe | `uart_ota_emit_startup_probe(void)` | Sends one-shot `OTA2: ready` on USART2 after boot so operators can confirm the OTA port and TX path |

### 3. Protocol Contract

All multi-byte fields are little-endian `uint32_t`.

| Frame | Direction | Size / Format | Required Behavior |
|-------|-----------|---------------|-------------------|
| START | PC to App | `magic=0xA55A5AA5`, `type=1`, `appVersion`, `firmwareSize`, `firmwareCRC32`, `headerCRC32` | App validates header CRC and size, erases `0x08073000`, initializes session, then ACKs |
| DATA | PC to App | `magic=0xA55A5AA5`, `type=2`, `seq`, `offset`, `length`, `chunkCRC32`, `chunk` | App validates sequence, offset, length, chunk CRC, first-chunk vector table, then writes chunk to Flash |
| END | PC to App | `magic=0xA55A5AA5`, `type=3`, `firmwareSize`, `firmwareCRC32` | App validates total size, running CRC, Flash readback CRC, writes BootLoader parameters, then ACKs and resets |
| ACK | App to PC | `magic=0xA55A5AA5`, `type=0x80|原帧类型`, `status`, `value0`, `value1` | PC must wait for status `0` before sending the next frame |

Current App-side limits:

| Constant | Current Value | Reason |
|----------|---------------|--------|
| `BSP_USART2_RX_BUFFER_SIZE` | `1024U` | Must hold one DATA frame, not a full firmware image |
| `UART_OTA_STREAM_CHUNK_SIZE` | `512U` | `512B` payload + `24B` DATA header fits safely in 1KB |
| `UART_OTA_DEFAULT_BAUDRATE` | `460800` | App, BootLoader, and PC tool default UART baudrate must match |
| `UART_OTA_DOWNLOAD_MAX_SIZE` | `52KB` | Internal Flash download buffer remains `0x08073000..0x0807FFFF` |
| App start | `0x0800D000` | BootLoader jumps here after copying |
| Parameter area | `0x0800C000` | App writes update flags; BootLoader reads after reset |

### 4. Flash Layout

| Region | Address | Size | Owner |
|--------|---------|------|-------|
| BootLoader | `0x08000000` | `48KB` | BootLoader image |
| Parameter area | `0x0800C000` | `4KB` | App writes update flags; BootLoader reads them after reset |
| App area | `0x0800D000` | `0x63000` | BootLoader writes final App image here |
| Download buffer | `0x08073000` | `52KB` | App writes received firmware chunks here before reset |

### 5. Boot Parameter Fields

When OTA succeeds, App must write these fields in the BootLoader-compatible parameter layout:

| Field | Required Value |
|-------|----------------|
| `magicWord` | `0x5AA5C33C` |
| `updateFlag` | `0x5A` |
| `updateStatus` | `0x01` |
| `appSize` | `firmwareSize` from START/END |
| `appCRC32` | `firmwareCRC32` from START/END |
| `appVersion` | `appVersion` from START |
| `appStartAddr` | `0x0800D000` |
| `appStackAddr` | Word at `0x0800D000` |
| `appEntryAddr` | Word at `0x0800D004` |

### 6. Validation & Error Matrix

| Check | Valid Condition | Failure Result | Required Behavior |
|-------|-----------------|----------------|-------------------|
| Magic prefix | First 4 bytes match `0xA55A5AA5` | Not an OTA frame, or wait for more bytes if partial prefix | USART2 is OTA-dedicated, so non-OTA data may be ignored directly |
| START length | `frame_length == 24` | `UART_OTA_RESULT_BAD_LENGTH` | ACK error; do not erase Flash |
| START header CRC | `CRC32(frame[0..19]) == headerCRC32` | `UART_OTA_RESULT_VERIFY_ERROR` | ACK error; keep BootLoader flags unchanged |
| Firmware size | `1 <= firmwareSize <= 52KB` | `UART_OTA_RESULT_BAD_LENGTH` | ACK error; do not write Flash |
| DATA length | `frame_length == 24 + length`, `1 <= length <= 512` | `UART_OTA_RESULT_BAD_LENGTH` | ACK error; keep current session state for operator retry/restart |
| DATA order | `seq == next_seq`, `offset == received_size` | `UART_OTA_RESULT_BAD_LENGTH` | ACK error; reject out-of-order or duplicated chunks |
| Chunk CRC | `CRC32(chunk) == chunkCRC32` | `UART_OTA_RESULT_VERIFY_ERROR` | ACK error; do not write the chunk |
| Vector table | First chunk MSP in SRAM; entry in App range and Thumb address | `UART_OTA_RESULT_BAD_VECTOR` | ACK error before accepting invalid firmware |
| Flash write | Chunk writes to `0x08073000 + offset` successfully | `UART_OTA_RESULT_FLASH_ERROR` | ACK error; do not advance offset |
| END size/CRC | END fields match START and all bytes received | `UART_OTA_RESULT_BAD_LENGTH` or `VERIFY_ERROR` | ACK error; do not write BootLoader parameters |
| Download writeback CRC | CRC32 at `0x08073000` equals firmware CRC | `UART_OTA_RESULT_VERIFY_ERROR` | ACK error; do not write BootLoader parameters |
| Parameter write | Full 4KB parameter area write succeeds | `UART_OTA_RESULT_FLASH_ERROR` | ACK error; do not reset if update flags were not written correctly |
| Success | Download and parameter writes both pass | `UART_OTA_RESULT_SUCCESS` | Send END ACK, print ready log, software-reset into BootLoader |

### 7. Good / Base / Bad Cases

| Case | Input | Expected Result |
|------|-------|-----------------|
| Good | `--mode send`, `Project.bin <= 52KB`, correct CRC and vector table | App prints `OTA: stream start ...` and `OTA: ready, reset to BootLoader`; BootLoader prints `app crc32 check pass` and `app update success` |
| Base | Normal USART0 data that does not start with streaming OTA magic | Data follows existing transparent forwarding behavior |
| Base | Normal USART2 data that does not start with streaming OTA magic | OTA task ignores it and does not pollute USART0/RS485 forwarding path |
| Base | USB-to-serial sends one START/DATA/END frame per IDLE receive | App consumes each frame and returns ACK before the next frame |
| Bad | Operator sends `Project.bin` directly | No OTA write is triggered because frame magic/type/header are missing |
| Bad | Operator sends legacy `Project.uota` directly | Current low-RAM streaming parser rejects it because magic is `0x5AA5C33C`, not `0xA55A5AA5` |
| Bad | `firmwareSize > 52KB` | App rejects START before writing firmware chunks |
| Bad | CRC mismatch or invalid vector table | App rejects and does not set BootLoader update flags |

### 8. Tests Required

Before committing OTA-related changes, run:

```powershell
python -m unittest tools.test_uart_ota_packet
python tools\make_uart_ota_packet.py --mode stream-info --version 0x00000003 --chunk-size 512
python tools\make_uart_ota_packet.py --version 0x00000003
& 'E:\Keil_v5\UV4\UV4.exe' -b 'MDK\2026706296.uvprojx' -j0
Select-String -Path 'MDK\output\Project.build_log.htm' -Pattern 'Program Size|Error\(s\)|Warning\(s\)'
& 'E:\Keil_v5\ARM\ARMCLANG\bin\fromelf.exe' --text -z 'MDK\output\Project.axf'
```

Required assertions:

| Assertion | Expected Evidence |
|-----------|-------------------|
| Python tests | `OK` from `tools.test_uart_ota_packet` |
| Stream info | Output contains firmware size, CRC, version, chunk size, and chunk count |
| Legacy packet mode | Still generates `Project.uota`, but docs must mark it as not recommended for current low-RAM OTA sending |
| Keil build | Build log reports `0 Error(s)` |
| RAM usage | `fromelf` shows `usart2_rxbuffer` and `uart_ota_dma_buffer` at `0x400` each, not `52KB+16B` |
| Hardware, when available | Use `--mode send --port COMx`; log must include `OTA: ready, reset to BootLoader`, then BootLoader `app crc32 check pass` and `app update success` |

### 9. Operator Procedure

Use this procedure whenever sending a new App image through USART2 streaming OTA.
Always increment `--version` for each hardware trial so the BootLoader log proves the
new image was actually installed.

| Step | Command / Action | Required Evidence |
|------|------------------|-------------------|
| 1 | Build the Keil target | Build log reports `0 Error(s)` and `MDK/output/Project.bin` is non-empty |
| 2 | `python tools\make_uart_ota_packet.py --mode stream-info --version <new_version> --chunk-size 512` | Output shows firmware size, CRC, version, chunk size, and chunk count |
| 3 | `python tools\make_uart_ota_packet.py --mode send --port <COMx> --version <new_version> --chunk-size 512` | PC prints stream metadata, `START/DATA/END acked` progress, then `sent stream frames=<n>` |
| 4 | Watch `USART2 (PD8/PD9)` during a fresh boot | App prints one-shot `OTA2: ready`, proving the OTA TX path and port selection are correct |
| 5 | Watch the debug UART on `USART0 (PA9/PA10)` | App prints `OTA: rx ...` when frames arrive, then `OTA: ready, reset to BootLoader` after END succeeds |
| 6 | Watch the BootLoader UART log after reset | BootLoader prints `app crc32 check pass`, `app update success`, and the new `appVersion` |

Current known-good hardware command for the local board:

```powershell
python tools\make_uart_ota_packet.py --mode send --port COM29 --baudrate 460800 --version 0x00000005 --chunk-size 512
```

For the next trial, keep the same port and chunk size unless the board wiring changes,
but increase the version, for example:

```powershell
python tools\make_uart_ota_packet.py --mode stream-info --version 0x00000006 --chunk-size 512
python tools\make_uart_ota_packet.py --mode send --port COM29 --baudrate 460800 --version 0x00000006 --chunk-size 512
```

Do not reuse an old version number when validating a bug fix. A repeated version makes it
hard to tell whether the observed behavior came from the new binary or the previously
installed App.

If the PC times out waiting for START ACK, apply this decision tree first:

| Observation | Meaning | Next Action |
|-------------|---------|-------------|
| `PD8/PD9` shows `OTA2: ready`, but Python still times out on START | App image is new enough and USART2 TX works | Check the PC RX wire, common ground, and whether the same COM port is really connected to `PD8/PD9` |
| `USART0` shows `OTA: rx ...` while Python times out | App received the START frame | Prioritize ACK return path debugging on `PD8` instead of RX parsing |
| `USART0` shows no `OTA: rx ...` during send | App never received a valid USART2 frame | Check wiring to `PD8/PD9`, baudrate, and whether the board is still running an old App without USART2 OTA |
| Operator expects `BOOT: start` on `PD8/PD9` | Wrong observation point | `BOOT: start` belongs to `USART0`; `USART2` only emits `OTA2: ready` at boot and binary ACK during OTA |

### 10. Wrong vs Correct

#### Wrong

```powershell
# Raw firmware has no streaming frame header, so App will not enter OTA mode.
send-file MDK\output\Project.bin
```

#### Wrong

```powershell
# Legacy full package can still be generated, but direct sending is not the current low-RAM OTA flow.
python tools\make_uart_ota_packet.py --version 0x00000002
send-file MDK\output\Project.uota
```

#### Correct

```powershell
# First check metadata, then send streaming frames and wait for ACK after every frame.
python tools\make_uart_ota_packet.py --mode stream-info --version 0x00000006 --chunk-size 512
python tools\make_uart_ota_packet.py --mode send --port COM29 --baudrate 460800 --version 0x00000006 --chunk-size 512
```

#### Wrong

```c
/* Reducing only the OTA C buffer without changing the PC sender can split one DATA frame. */
#define BSP_USART2_RX_BUFFER_SIZE 128U
```

#### Correct

```c
/* 512B DATA 载荷 + 24B 协议头可安全放入 1024B OTA DMA 缓冲。 */
#define BSP_USART2_RX_BUFFER_SIZE 1024U
```

---

## Common Mistakes

- Do not send `Project.bin`, `Project.hex`, or legacy `Project.uota` directly for the current low-RAM OTA flow.
- Do not reduce `BSP_USART2_RX_BUFFER_SIZE` below the largest DATA frame size plus header.
- Do not raise `--chunk-size` above `UART_OTA_STREAM_CHUNK_SIZE` unless App and tests are updated together.
- Do not change App, BootLoader, and PC sender baudrates independently; the three defaults must stay aligned.
- Do not move `0x0800C000`, `0x0800D000`, or `0x08073000` in one layer only.
- Do not reset after writing the download buffer if BootLoader parameter flags were not written.
- Do not claim App images larger than `52KB` are supported until the download-buffer storage is redesigned.
- Do not reuse the same `--version` while validating a fix; increment it so the BootLoader log confirms the tested image.
