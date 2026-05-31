# Embedded OTA Guidelines

> Scope: RS485/USART1 App-side header-bin OTA flow for the GD32F470 standalone BootLoader project at `D:\GD32\2026706296_bootloader`.

---

## Scenario: RS485/USART1 Header-Bin OTA

### 1. Scope / Trigger

Use this guideline whenever changing:

| Area | Files / Entries |
|------|-----------------|
| App OTA parser | `Function/uart_ota_app.c`, `Function/uart_ota_app.h` |
| Boot handoff helper | `HardWare/BOOTLOADER/bootloader_port.c`, `HardWare/BOOTLOADER/bootloader_port.h` |
| RS485/USART1 DMA handoff | `User/gd32f4xx_it.c`, `HardWare/USART/bsp_usart.h`, `HardWare/USART/bsp_usart.c` |
| OTA image packer | `tools/pack_ota_image.c`, `tools/test_header_bin_ota_static.py`, Keil `AfterMake` command |
| Boot handoff | BootLoader/App Flash partition constants, parameter layout, or CRC logic |

This is a cross-layer contract. The Keil post-build packer, App-side raw receiver, internal Flash layout, and BootLoader parameter reader must agree exactly.

### 2. Signatures

| Boundary | Signature / Entry | Contract |
|----------|-------------------|----------|
| Keil raw App bin | `E:\Keil_v5\ARM\ARMCLANG\bin\fromelf.exe --bin --output=.\output\Project.bin .\output\Project.axf` | Generates the plain App payload whose first word is MSP and second word is Reset_Handler |
| OTA packer | `tools\pack_ota_image.exe .\output\Project.bin .\output\Project_ota.bin 0x00000001 0x0800D000` | Prepends a 64-byte header with magic, size, load address, version, payload CRC32, header CRC32, MSP, and Reset_Handler |
| Operator file | `project/output/Project_ota.bin` | This is the only file sent through RS485/USART1 for this branch's OTA flow |
| UART receiver | `uart_ota_feed_rx_bytes(const uint8_t *data, uint16_t length)` | Consumes raw bytes from the task layer, first parsing the header, then collecting payload bytes |
| ISR handoff | `USART1_IRQHandler(void)` and `DMA0_Channel5_IRQHandler(void)` | Copy DMA bytes into `uart_ota_dma_buffer`, set `uart_ota_rx_flag`, and re-arm DMA; no CRC, logging, or Flash writes in ISR |
| Task polling | `uart_ota_task(void)` | Takes DMA chunks, feeds raw bytes to the OTA parser, and commits payload to Flash only after full payload CRC passes |
| Wiring probe | `uart_ota_emit_startup_probe(void)` | Sends one-shot `OTA485: ready, send Project_ota.bin raw` after boot so operators can confirm the RS485 port |

### 3. Protocol Contract

The operator sends `Project_ota.bin` as raw bytes. There is no extra sender script, no legacy frame wrapping, no terminal-managed file-transfer mode, no receiver polling character, and no per-frame acknowledgement loop.

All multi-byte fields are little-endian `uint32_t`.

| Offset | Field | Required Value |
|--------|-------|----------------|
| `0x00` | `magic` | `0x474F5441` |
| `0x04` | `header_size` | `64` |
| `0x08` | `image_size` | App payload size, `1..100KB` |
| `0x0C` | `load_addr` | `0x0800D000` |
| `0x10` | `version` | App version written to BootLoader parameter area |
| `0x14` | `image_crc32` | CRC32 over App payload only |
| `0x18` | `flags` | `0` |
| `0x1C` | `header_crc32` | CRC32 over the 64-byte header with this field set to `0` |
| `0x20` | `stack_addr` | Payload word 0, must be in SRAM |
| `0x24` | `entry_addr` | Payload word 1, must be a Thumb address inside App region |
| `0x28..0x3F` | reserved | `0` for current packer output |

### 4. Flash And RAM Layout

| Region | Address / Size | Owner | Contract |
|--------|----------------|-------|----------|
| BootLoader | `0x08000000`, `48KB` | BootLoader image | MCU reset runs here first |
| Parameter area | `0x0800C000`, `4KB` | App writes, BootLoader reads | Holds update flags, size, CRC, and version |
| App area | `0x0800D000`, `0x5A000` | BootLoader writes final App | Must match Keil IROM and header `load_addr` |
| Download buffer | `0x08067000`, `100KB` | App writes received payload | BootLoader copies from here after reset |
| USART1 DMA window | `BSP_USART1_RX_BUFFER_SIZE = 1024U` | ISR handoff | Raw stream chunk size, not a full image buffer |
| ISR-to-task queue | `UART_OTA_RX_QUEUE_DEPTH = 4U` | ISR writes, task reads | Absorbs short scheduler delays during continuous raw file send |
| OTA RAM payload buffer | `UART_OTA_PAYLOAD_BUFFER_SIZE = 100KB` | App OTA task | Receives the full payload before Flash erase/write |

The full-payload RAM buffer is intentional. It avoids erasing/writing internal Flash while the PC is still sending bytes, which would risk losing UART data because internal Flash operations can stall code execution.

### 5. Boot Parameter Fields

When OTA succeeds, App must write these fields in the BootLoader-compatible parameter layout:

| Field | Required Value |
|-------|----------------|
| `magicWord` | `0xC0DEF47A` |
| `updateFlag` | `0x5A` |
| `updateStatus` | `0x01` |
| `appSize` | Header `image_size` |
| `appCRC32` | Header `image_crc32` |
| `appVersion` | Header `version` |
| `appStartAddr` | `0x0800D000` |

BootLoader copies `appSize` bytes from `0x08067000` to `0x0800D000`, then recalculates CRC over the final App area and compares it with `appCRC32`.

### 6. Validation & Error Matrix

| Check | Valid Condition | Failure Result | Required Behavior |
|-------|-----------------|----------------|-------------------|
| Header magic | `magic == 0x474F5441` | Not a valid OTA image | Enter error state; do not erase Flash |
| Header size | `header_size == 64` | Unsupported image format | Enter error state; do not erase Flash |
| Payload size | `1 <= image_size <= 100KB` | Exceeds internal download buffer/RAM buffer | Enter error state; do not erase Flash |
| Load address | `load_addr == 0x0800D000` | Would write wrong App region | Enter error state; do not erase Flash |
| Header CRC | CRC32(header with `header_crc32=0`) matches | Corrupt header | Enter error state; do not erase Flash |
| Stack address | `0x20000000 <= stack_addr < 0x20030000` | Invalid vector table | Enter error state; do not erase Flash |
| Entry address | Thumb address inside App area | Invalid vector table | Enter error state; do not erase Flash |
| Payload CRC | CRC32(payload) matches `image_crc32` | Corrupt payload | Enter error state; do not erase Flash |
| Payload vector | Payload word 0/1 match header `stack_addr/entry_addr` and pass vector validation | Header/payload mismatch | Enter error state; do not erase Flash |
| Download writeback CRC | CRC32 at `0x08067000` matches `image_crc32` | Flash write failure or stale data | Do not write BootLoader flags |
| Parameter write | Full 4KB parameter area write succeeds | BootLoader would not know about the update | Do not reset into BootLoader |
| Success | Download and parameter writes both pass | Ready for BootLoader | Print ready log and software-reset |

### 7. Good / Base / Bad Cases

| Case | Input | Expected Result |
|------|-------|-----------------|
| Good | Raw send `project/output/Project_ota.bin`, payload `<= 100KB`, valid header and vector table | App prints `OTA: header ok`, `OTA: payload ok`, `OTA: ready, reset to BootLoader`; BootLoader prints `app crc32 check pass` and `app update success` |
| Base | Normal USART0 debug command | Handled by `uart_task()` and does not affect RS485 OTA state |
| Base | RS485 receives bytes that do not start with the OTA magic | App enters OTA error state and does not erase Flash |
| Bad | Raw send `Project.bin` instead of `Project_ota.bin` | Header magic fails; no Flash erase/write |
| Bad | Send an obsolete packaged stream instead of `Project_ota.bin` raw bytes | Header magic fails; no Flash erase/write |
| Bad | Payload exceeds `100KB` | Header size check fails |
| Bad | CRC mismatch or invalid vector table | App rejects and does not set BootLoader update flags |

### 8. Tests Required

Before committing OTA-related changes, run:

```powershell
gcc -std=c99 -Wall -Wextra -Werror tools\pack_ota_image.c -o tools\pack_ota_image.exe
python -m unittest tools.test_header_bin_ota_static
& 'E:\Keil_v5\UV4\UV4.exe' -b 'project\2026706296.uvprojx' -j0
Select-String -Path 'project\output\Project.build_log.htm' -Pattern 'Program Size|Error\(s\)|Warning\(s\)'
Test-Path 'project\output\Project.bin'
Test-Path 'project\output\Project_ota.bin'
& 'E:\Keil_v5\ARM\ARMCLANG\bin\fromelf.exe' --text -z 'project\output\Project.axf'
Select-String -Path 'project\Listings\Project.map' -Pattern '__use_no_semihosting|_sys_open|_sys_write|_sys_exit|_ttywrch'
```

Required assertions:

| Assertion | Expected Evidence |
|-----------|-------------------|
| Packer builds | `gcc` exits with status 0 |
| Header-bin static contract | `tools.test_header_bin_ota_static` exits with status 0 |
| Keil build | Build log reports `0 Error(s)` |
| Raw App output | `project/output/Project.bin` exists and is non-empty |
| OTA image output | `project/output/Project_ota.bin` exists and is exactly 64 bytes larger than `Project.bin` |
| No semihosting | Map shows `__use_no_semihosting`, and `_sys_open/_sys_write/_sys_exit/_ttywrch` resolve to `main.o` |
| Hardware, when available | Raw-send `Project_ota.bin`; logs include App `OTA: ready` and BootLoader `app crc32 check pass` |

### 9. Operator Procedure

Use this procedure whenever sending a new App image through RS485/USART1 OTA.

| Step | Command / Action | Required Evidence |
|------|------------------|-------------------|
| 1 | Build the Keil target | Build log reports `0 Error(s)` |
| 2 | Confirm generated files | `project/output/Project.bin` and `project/output/Project_ota.bin` both exist |
| 3 | Open a serial tool on the RS485/USART1 COM port at `460800 8N1` | Fresh boot shows `OTA485: ready, send Project_ota.bin raw` |
| 4 | Use the serial tool's raw/direct file-send mode | Select `project/output/Project_ota.bin`; do not select YModem/XModem |
| 5 | Watch USART0 debug logs | App prints `OTA: header ok`, `OTA: payload ok`, and `OTA: ready, reset to BootLoader` |
| 6 | Watch BootLoader UART logs after reset | BootLoader prints `app crc32 check pass` and `app update success` |

### 10. Wrong vs Correct

#### Wrong

```text
Terminal-managed file transfer -> project/output/Project.bin
```

#### Correct

```text
Raw/direct file send -> project/output/Project_ota.bin
```

#### Wrong

```powershell
python tools\make_uart_ota_packet.py --mode send --port COM29 --baudrate 460800
```

#### Correct

```powershell
tools\pack_ota_image.exe project\output\Project.bin project\output\Project_ota.bin 0x00000001 0x0800D000
```

#### Wrong

```c
/* Only IDLE handoff is not enough for continuous raw file send. */
usart_interrupt_enable(USART1, USART_INT_IDLE);
```

#### Correct

```c
/* Raw file send needs both IDLE and DMA full-transfer handoff. */
usart_interrupt_enable(USART1, USART_INT_IDLE);
dma_interrupt_enable(USART1_RX_DMA_PERIPH, USART1_RX_DMA_CHANNEL, DMA_INT_FTF);
```

---

## Common Mistakes

- Do not send `Project.bin`; send `Project_ota.bin`.
- Do not use terminal-managed file-transfer modes for this branch's OTA flow; use raw/direct file send.
- Do not reintroduce helper senders or legacy frame wrapping as an operator requirement.
- Do not reduce `BSP_USART1_RX_BUFFER_SIZE` or `UART_OTA_RX_QUEUE_DEPTH` without validating DMA full-transfer cadence.
- Do not remove the 100KB RAM payload buffer unless you redesign reception around a real ring buffer or external storage.
- Do not erase/write internal Flash while the PC is still streaming bytes.
- Do not move `0x0800C000`, `0x0800D000`, or `0x08067000` in one layer only.
- Do not reset after writing the download buffer if BootLoader parameter flags were not written.
- Do not claim App payloads larger than `100KB` are supported until the download-buffer storage is redesigned.
- Do not diagnose a standalone hang after `BootLoader : jump app ...` as a BootLoader address issue before checking for AC6 semihosting `BKPT 0xAB` in the App.

---

## Scenario: BootLoader Handoff Standalone Hang

### 1. Scope / Trigger

Use this scenario whenever the device boots through BootLoader and the serial log
stops after a line like:

```text
BootLoader : jump app vtor:0x0800d000 msp:0x20005818 entry:0x0800d379
```

This is a BootLoader-to-App boundary bug class. The failure may be in the App C
runtime, linker map, interrupt/vector handoff, or BootLoader cleanup path. Do
not modify vendor `SystemInit()` or BootLoader jump addresses until the evidence
below identifies that layer.

Root-cause category for the May 2026 incident:

| Category | Classification | Specific Cause |
|----------|----------------|----------------|
| Cross-layer contract | BootLoader handoff to App C runtime | BootLoader jumped correctly, but ARMCLANG C library initialization entered semihosting before `main()` |
| Test coverage gap | Debugger-only validation differed from standalone reset | Keil debugger could continue past `BKPT 0xAB`, hiding the standalone HardFault/hang |
| Implicit assumption | `printf()` retarget was assumed sufficient | C library also opens standard streams through `_sys_open()` during `__rt_lib_init` |

### 2. Signatures

| Boundary | Signature / Evidence | Contract |
|----------|----------------------|----------|
| BootLoader vector read | `appStackAddr:<u32>`, `appEntryAddr:<u32>`, `appStartAddr:0x0800d000` | MSP must be in SRAM and Reset_Handler must be a Thumb address inside App flash |
| BootLoader final handoff log | `BootLoader : jump app vtor:<addr> msp:<addr> entry:<addr>` | This log means BootLoader has reached the last pre-jump checkpoint |
| App first log | `BOOT: handoff start` | This proves execution reached App code after C runtime startup |
| Debugger trap | `BKPT 0xAB` with `_sys_open -> freopen -> __rt_lib_init` | Treat as ARM semihosting leakage, not as a BootLoader address failure |
| Link map | `Select-String -Path 'project\Listings\Project.map' -Pattern '__use_no_semihosting|_sys_open|_sys_write|_sys_exit|_ttywrch'` | Retarget symbols must resolve to `main.o` and `__use_no_semihosting` must exist |

### 3. Contracts

| Contract | Required Implementation | Why |
|----------|-------------------------|-----|
| App vector table | App image starts at `0x0800D000`, with valid SRAM MSP and Thumb Reset_Handler | BootLoader can only jump safely when the first two vector words are valid |
| BootLoader cleanup | Disable SysTick, clear pending interrupts, set `SCB->VTOR`, set MSP, then branch to App Reset_Handler | App must not inherit active BootLoader interrupt state |
| App runtime | ARMCLANG builds must provide `__use_no_semihosting` | Prevent C library semihosting calls during `__rt_lib_init` |
| Retarget stubs | `User/main.c` owns `_sys_open`, `_sys_write`, `_sys_read`, `_sys_exit`, `_ttywrch`, and `fputc` | Standard streams must be resolved inside firmware, not through debugger services |
| Early UART safety | Retarget write path must drop characters until USART0 is initialized | C runtime can write before `system_init()` configures the UART |
| App handoff init | `boot_app_handoff_init()` must run before SysTick/peripheral interrupt use | App must reclaim VTOR, pending interrupt state, and global interrupt enable |

### 4. Validation & Error Matrix

| Observation | Meaning | Required Next Step |
|-------------|---------|--------------------|
| Parameter area reads as `0xFF`, but App vector is valid | No OTA task is pending; this is not itself a hang root cause | Continue to App handoff diagnosis |
| `jump app` log is absent | BootLoader has not reached the handoff point | Debug BootLoader parameter parsing and vector validation |
| `jump app` log exists, but `BOOT: handoff start` is absent | Failure is after BootLoader's final checkpoint and before App first log | Inspect debugger PC/call stack before editing BootLoader |
| Debugger stops at `BKPT 0xAB` | Semihosting leakage | Fix/restore App retarget stubs and `__use_no_semihosting` |
| Map resolves `_sys_open` to a C library object instead of `main.o` | Retarget contract is broken | Rebuild after adding or restoring the stub in `User/main.c` |
| App logs `BOOT: handoff start` but then hangs | C runtime handoff is past; next issue is App init order or peripheral driver | Use `BOOT:` stage logs to isolate the failing peripheral |

### 5. Good / Base / Bad Cases

| Case | Input / Situation | Expected Result |
|------|-------------------|-----------------|
| Good | Standalone reset after BootLoader and App are flashed | Logs progress from `jump app` to `BOOT: handoff start` and later App init logs |
| Good | Keil debug run with no semihosting leakage | No stop at `BKPT 0xAB`; map symbols resolve to `main.o` |
| Base | Empty parameter area, valid App vector table | BootLoader prints `param magic is false`, then still jumps to the existing App |
| Base | App starts directly under debugger | App still runs because `boot_app_handoff_init()` reclaims VTOR and interrupt state |
| Bad | Debugger stops at `BKPT 0xAB` before `main()` | The image is not standalone-safe and must not be accepted as fixed |
| Bad | Only debugger-run behavior is tested | The fix is incomplete because the original failure was standalone reset |

### 6. Tests Required

Run these checks after any change to BootLoader handoff, App startup,
`User/main.c`, compiler options, or `printf()` retargeting:

```powershell
& 'E:\Keil_v5\UV4\UV4.exe' -b 'project\2026706296.uvprojx' -j0
Select-String -Path 'project\output\Project.build_log.htm' -Pattern 'Program Size|Error\(s\)|Warning\(s\)'
Select-String -Path 'project\Listings\Project.map' -Pattern '__use_no_semihosting|_sys_open|_sys_write|_sys_exit|_ttywrch'
```

Required assertions:

| Assertion | Expected Evidence |
|-----------|-------------------|
| Build status | Build log reports `0 Error(s)` |
| No semihosting | Map includes `__use_no_semihosting` |
| Retarget ownership | `_sys_open`, `_sys_write`, `_sys_exit`, and `_ttywrch` resolve to `main.o` |
| Hardware standalone reset | Serial log reaches `BOOT: handoff start` without a debugger attached |
| Debugger confirmation when needed | If attached, PC must not stop at `BKPT 0xAB` during `__rt_lib_init` |

### 7. Wrong vs Correct

#### Wrong

```c
/* Guessing the App start address is wrong because the log already proved the vector table was read. */
#define BOOT_APP_START_ADDR 0x08000000U
```

#### Correct

```powershell
# Verify whether the App image is standalone-safe before changing handoff addresses.
Select-String -Path 'project\Listings\Project.map' -Pattern '__use_no_semihosting|_sys_open|_sys_write|_sys_exit|_ttywrch'
```

#### Wrong

```c
/* Only retargeting fputc is incomplete for ARMCLANG standard stream startup. */
int fputc(int ch, FILE *f)
{
    return ch;
}
```

#### Correct

```c
/*
 * ARMCLANG standalone App must disable semihosting and provide _sys_* stubs,
 * because C runtime initialization can open standard streams before main().
 */
__asm(".global __use_no_semihosting\n");
```

### 8. Prevention Notes

| Priority | Mechanism | Action | Status |
|----------|-----------|--------|--------|
| P0 | Code-spec | Keep this scenario and map-symbol checks in the OTA handoff spec | Done |
| P0 | Review checklist | Any App startup or logging change must ask "does standalone reset still pass?" | Done |
| P1 | Hardware validation | Prefer power-cycle or reset-button validation over debugger-only validation for boot bugs | Required when hardware is available |
| P1 | Diagnostic logging | Keep `jump app` and `BOOT: handoff start` as the boundary markers | Done |
