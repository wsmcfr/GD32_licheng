# Quality Guidelines

> Code quality standards for the formal CIMC GD32F470 firmware build.

---

## Overview

Formal firmware quality is maintained through:

- strict module boundaries between `Driver/`, `Protocol/`, `Function/`, and `User/`
- explicit ownership for USART1/RS485, ADC/DAC, RTC, OLED, and Bootloader handoff
- bounded buffers and ISR-to-task handoff
- synchronized user documentation whenever commands, addresses, baud rates, or build steps change
- Keil rebuild evidence for both App and Bootloader

---

## Formal Build Must Not Reintroduce Removed Features

The formal Keil target must not compile these legacy entries:

| Removed Area | Forbidden Build Entries |
|--------------|-------------------------|
| USART0 shell / debug receiver | `bsp_usart0_init`, `USART0_IRQHandler`, `usart0_rxbuffer` |
| USART5 | `bsp_usart5_init`, `USART5_IRQHandler`, `usart5_rxbuffer` |
| Button workflow | `Function/btn_app.c`, `Driver/KEY/bsp_key.c` |
| Button low-power demo | `Driver/POWER/bsp_power.c` |
| Old App-side OTA | `Function/uart_ota_app.c`, `Protocol/ota_image_protocol.c` |
| External file systems | `Driver/GD25QXX/smartfs_port.c`, `lfs.c`, `lfs_util.c` |
| Old header BIN packer | `tools/pack_ota_image.c`, `tools/pack_ota_image.exe`, `tools/test_header_bin_ota_static.py`, `Project_ota.bin` after-build flow |

The formal source tree physically removes these legacy modules. If any of them
reappears, treat it as a regression unless a new requirement explicitly restores
that capability.

---

## Required Runtime Contracts

| Area | Contract |
|------|----------|
| Official communication | USART1/RS485 only, default `19200 8N1` |
| Frame format | ASCII HEX text encoding binary `A5B6 ... B6A5` frames |
| CRC | CRC-16-Modbus over binary bytes from frame header through payload, sent big-endian |
| ISR work | `USART1_IRQHandler()` only captures length, copies bounded bytes, sets `rx_flag`, and re-arms DMA |
| Protocol work | `uart_task()` calls `cimc_protocol_process_ascii_frame()` in task context |
| Logging | No App debug output API; retarget stubs directly discard bytes and never touch USART1/RS485 |
| OLED | Formal App display is two lines: team ID and `AutoSample` / `IDLE` |
| LED | Formal App uses two LEDs: LED1 system blink, LED2 auto-sample status |
| DAC | `0x0301` exclusively controls DAC0 OUT0; ADC periodic logic must not overwrite it |
| OTA | App handles `0x0501`; Bootloader handles `0x0502/0x0503` and `5AA5C33C` bin magic |
| Build output | App after-build generates `Project.bin` only; no `Project_ota.bin` |

---

## Forbidden Patterns

### Heavy Work In Interrupt Handlers

Do not parse CRC, format strings, erase/write Flash, reset the MCU, or execute
business logic from ISR context. Keep interrupt handlers bounded and predictable.

### Stale Workflow Documentation

When changing baud rates, Flash addresses, command IDs, partition sizes, generated
artifacts, or operator steps, update repository Markdown and `.trellis/spec/` in
the same task.

Examples of stale formal workflow defects:

| Stale Text | Required Fix |
------------|--------------|
| `115200` for formal RS485 | Replace with `19200` unless explicitly documenting an upstream example |
| `Project_ota.bin` for formal OTA | Replace with contest `0x0501 -> 0x0502 -> bin -> 0x0503` |
| USART0 debug shell | Mark as removed legacy behavior |
| SMARTFS/littlefs startup self-test | Mark as removed from formal build |

### Unbounded Buffer Operations

Use explicit size macros and bounds before copying:

- `UART_APP_DMA_BUFFER_SIZE`
- `BSP_USART1_RX_BUFFER_SIZE`
- `OLED_APP_LINE_BUFFER_SIZE`
- `CIMC_PROTOCOL_BINARY_BUFFER_SIZE`

### Hardware Constants Without Impact Search

Before changing pins, DMA channels, baud rates, Flash addresses, or buffer sizes,
search for all dependent usages.

---

## RS485 Direction-Control Quality Gate

RS485 receive success does not prove the direction GPIO is correct. For the
formal board, PE8 controls MAX3485 `DE/RE#`.

Expected contract:

```c
#define RS485_USART                    USART1
#define RS485_DIR_PORT                 GPIOE
#define RS485_DIR_CLK_PORT             RCU_GPIOE
#define RS485_DIR_PIN                  GPIO_PIN_8
#define RS485_DIR_TX_LEVEL             SET
#define RS485_DIR_RX_LEVEL             RESET
```

Validation before blaming protocol code:

| Observation | Required Check |
-------------|----------------|
| MCU receives but peer cannot receive | Probe PE8, USART1_TX, and RS485 A/B |
| USART1 TX waveform exists but A/B idle | Check MAX3485 direction pin and polarity |
| Direction macro changed | Verify schematic/continuity, then update docs |

---

## Keil After-Build Contract

Formal App after-build must be:

```xml
<RunUserProg1>1</RunUserProg1>
<RunUserProg2>0</RunUserProg2>
<UserProg1Name>E:\Keil_v5\ARM\ARMCLANG\bin\fromelf.exe --bin --output=.\output\Project.bin .\output\Project.axf</UserProg1Name>
<UserProg2Name></UserProg2Name>
```

Required artifacts:

| Artifact | Contract |
|----------|----------|
| `project/output/Project.axf` | Main link output |
| `project/output/Project.hex` | Addressed image for direct programming |
| `project/output/Project.bin` | Raw App image; non-empty |
| `project/output/Project_ota.bin` | Not required and must not be part of formal workflow |

---

## Required Verification

Run before claiming the formal build is ready:

```powershell
& 'E:\Keil_v5\UV4\UV4.exe' -b 'project\2026706296.uvprojx' -j0
Select-String -Path 'project\output\Project.build_log.htm' -Pattern 'Program Size|Error\(s\)|Warning\(s\)'
Test-Path 'project\output\Project.bin'

Push-Location 'D:\GD32\2026706296_bootloader'
& 'E:\Keil_v5\UV4\UV4.exe' -b 'project\2026706296.uvprojx' -j0
Select-String -Path 'project\Objects\2026706296.build_log.htm' -Pattern 'Program Size|Error\(s\)|Warning\(s\)'
Pop-Location

rg -n "btn_app\.c|bsp_key\.c|bsp_power\.c|uart_ota_app\.c|ota_image_protocol\.c|smartfs_port\.c|lfs\.c|lfs_util\.c|pack_ota_image" project\2026706296.uvprojx
git diff --check
```

Expected evidence:

- App build reports `0 Error(s), 0 Warning(s)`.
- Bootloader build reports `0 Error(s), 0 Warning(s)`.
- `Project.bin` exists and is non-empty.
- Removed legacy files are absent from the App Keil compile list.
- `git diff --check` reports no whitespace errors.

---

## Code Review Checklist

- Is the code in the right layer?
- Are private helpers `static`?
- Are return codes checked immediately?
- Are ISR copy lengths bounded?
- Does the change preserve USART1/RS485 `19200` as the formal default?
- Does DAC remain controlled only by `0x0301`?
- Did docs/specs change with any user-visible behavior?
- Are App and Bootloader build logs verified?
