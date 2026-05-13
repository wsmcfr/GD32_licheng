# Embedded OTA Guidelines

> Scope: USART0 App-side OTA flow for the GD32F470 BootLoader_Two_Stage integration.

---

## Scenario: USART0 App-Side OTA Package

### 1. Scope / Trigger

Use this guideline whenever changing:

- `USER/App/usart_app.c` OTA parsing, CRC, Flash write, or reset flow
- `USER/gd32f4xx_it.c` USART0 IDLE + DMA handoff behavior
- `USER/Driver/bsp_usart.h` USART0 DMA buffer sizes
- `tools/make_uart_ota_packet.py` packet generation
- BootLoader/App Flash partition constants or parameter layout

This is a cross-layer contract. The PC-side packet generator, App-side receiver, internal Flash layout, and BootLoader parameter reader must agree exactly.

### 2. Signatures

| Boundary | Signature / Entry | Contract |
|----------|-------------------|----------|
| PC tool | `python tools\make_uart_ota_packet.py --version <u32>` | Reads `MDK/output/Project.bin`, writes `MDK/output/Project.uota` |
| Packet format | `struct.pack("<IIII", magic, version, size, crc32) + firmware` | All four header fields are little-endian `uint32_t` |
| App parser | `prv_uart_ota_try_process_packet(const uint8_t *packet, uint32_t packet_length)` | Returns `uart_ota_result_t`; only resets after download-buffer and parameter writes pass |
| ISR handoff | `USART0_IRQHandler(void)` | Appends DMA chunks into `uart_dma_buffer` and sets `rx_flag` |
| Task polling | `uart_task(void)` | Processes `rx_flag`, handles OTA before normal USART0-to-RS485 forwarding |

### 3. Contracts

#### Packet Header

| Offset | Field | Type | Required Value / Meaning |
|--------|-------|------|--------------------------|
| `0x00` | `magic` | little-endian `uint32_t` | `0x5AA5C33C` |
| `0x04` | `appVersion` | little-endian `uint32_t` | Monotonic application version chosen by operator |
| `0x08` | `firmwareSize` | little-endian `uint32_t` | Raw `Project.bin` byte count, `1..52KB` |
| `0x0C` | `firmwareCRC32` | little-endian `uint32_t` | CRC32 of raw `Project.bin` bytes |
| `0x10` | `firmware` | byte array | Raw `Project.bin` linked for `0x0800D000` |

#### Flash Layout

| Region | Address | Size | Owner |
|--------|---------|------|-------|
| BootLoader | `0x08000000` | `48KB` | BootLoader image |
| Parameter area | `0x0800C000` | `4KB` | App writes update flags; BootLoader reads them after reset |
| App area | `0x0800D000` | `0x63000` | BootLoader writes final App image here |
| Download buffer | `0x08073000` | `52KB` | App writes received firmware here before reset |

#### Boot Parameter Fields

When OTA succeeds, App must write these fields in the BootLoader-compatible parameter layout:

| Field | Required Value |
|-------|----------------|
| `magicWord` | `0x5AA5C33C` |
| `updateFlag` | `0x5A` |
| `updateStatus` | `0x01` |
| `appSize` | `firmwareSize` from packet header |
| `appCRC32` | `firmwareCRC32` from packet header |
| `appVersion` | `appVersion` from packet header |
| `appStartAddr` | `0x0800D000` |
| `appStackAddr` | Word at `0x0800D000` |
| `appEntryAddr` | Word at `0x0800D004` |

### 4. Validation & Error Matrix

| Check | Valid Condition | Failure Result | Required Behavior |
|-------|-----------------|----------------|-------------------|
| Magic prefix | First bytes match little-endian `0x5AA5C33C` | Not a packet or wait more | Preserve normal serial forwarding unless prefix indicates partial OTA |
| Packet length | `packet_length == 16 + firmwareSize` | Bad length or wait more | Do not write Flash until complete package is present |
| Firmware size | `1 <= firmwareSize <= 52KB` | `UART_OTA_RESULT_BAD_LENGTH` | Reject and keep BootLoader parameter flags unchanged |
| Vector table | MSP in SRAM; entry in App range and Thumb address | `UART_OTA_RESULT_BAD_VECTOR` | Reject before erasing download buffer |
| Header CRC | CRC32(raw firmware) equals header CRC | `UART_OTA_RESULT_VERIFY_ERROR` | Reject before writing BootLoader parameters |
| Download writeback CRC | CRC32 at `0x08073000` equals firmware CRC | `UART_OTA_RESULT_VERIFY_ERROR` | Reject before writing BootLoader parameters |
| Parameter write | Full 4KB parameter area write succeeds | `UART_OTA_RESULT_FLASH_ERROR` | Do not reset if update flags were not written correctly |
| Success | Download and parameter writes both pass | `UART_OTA_RESULT_SUCCESS` | Print ready log and software-reset into BootLoader |

### 5. Good / Base / Bad Cases

| Case | Input | Expected Result |
|------|-------|-----------------|
| Good | Fresh `Project.uota`, size below `52KB`, correct CRC and vector table | App prints `OTA: ready, reset to BootLoader`; BootLoader prints `app crc32 check pass` and `app update success` |
| Base | Normal USART0 data that does not start with OTA magic | Data follows existing transparent forwarding behavior |
| Base | USB-to-serial splits `.uota` into multiple IDLE chunks | ISR accumulates chunks until full packet is present |
| Bad | Operator sends `Project.bin` directly | No OTA write is triggered because magic/header are missing |
| Bad | `firmwareSize > 52KB` | App rejects before writing Flash |
| Bad | CRC mismatch | App rejects and does not set BootLoader update flags |
| Bad | Invalid vector table | App rejects before erasing download buffer |

### 6. Tests Required

Before committing OTA-related changes, run:

```powershell
python tools\make_uart_ota_packet.py --version 0x00000002
& 'E:\Keil_v5\UV4\UV4.exe' -b 'MDK\2026706296.uvprojx' -j0
Select-String -Path 'MDK\output\Project.build_log.htm' -Pattern 'Error\(s\)|Warning\(s\)'
```

Required assertions:

- Packet tool prints output path, total size, firmware size, CRC32, and version.
- Keil build log reports `0 Error(s)`.
- If hardware is available, send `MDK/output/Project.uota`, not `Project.bin`.
- Hardware log must include `OTA: ready, reset to BootLoader`, then BootLoader `app crc32 check pass` and `app update success`.

### 7. Wrong vs Correct

#### Wrong

```powershell
# Sends raw firmware. The App-side OTA parser will not see the required header.
send-file MDK\output\Project.bin
```

#### Correct

```powershell
# Generate the App-side OTA packet first, then send the .uota file.
python tools\make_uart_ota_packet.py --version 0x00000002
send-file MDK\output\Project.uota
```

#### Wrong

```c
/* Magic value is right, but field byte order is host-dependent and not explicit. */
fwrite(&header, sizeof(header), 1, output);
```

#### Correct

```python
# Header byte order is explicit and matches App-side little-endian parsing.
packet = struct.pack("<IIII", UART_OTA_MAGIC, app_version, len(firmware), firmware_crc32) + firmware
```

---

## Common Mistakes

- Do not reduce `BSP_USART0_RX_BUFFER_SIZE` below `52KB + 16B` without changing the OTA transport model.
- Do not move `0x0800C000`, `0x0800D000`, or `0x08073000` in one layer only.
- Do not reset after writing the download buffer if BootLoader parameter flags were not written.
- Do not treat `.bin`, `.hex`, and `.uota` as interchangeable files.
