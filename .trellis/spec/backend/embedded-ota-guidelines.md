# Embedded OTA Guidelines

> Scope: CIMC contest OTA flow for the GD32F470 App project at `D:\GD32\2026706296` and the standalone Bootloader project at `D:\GD32\2026706296_bootloader`.

---

## Scenario: CIMC 0x0501 / 0x0502 / 0x0503 OTA

### 1. Scope / Trigger

Use this guideline whenever changing:

| Area | Files / Entries |
|------|-----------------|
| App contest protocol | `Protocol/cimc_protocol.c`, `Protocol/cimc_protocol.h` |
| App boot handoff helper | `Driver/BOOTLOADER/bootloader_port.c`, `Driver/BOOTLOADER/bootloader_port.h` |
| App RS485 handoff | `User/gd32f4xx_it.c`, `Driver/USART/bsp_usart.c`, `Driver/USART/bsp_usart.h`, `Function/usart_app.c` |
| Bootloader contest protocol | `D:\GD32\2026706296_bootloader\Protocol\boot_cimc_protocol.c`, `.h` |
| Bootloader main flow | `D:\GD32\2026706296_bootloader\Function\Function.c` |
| Flash partition constants | App/Bootloader parameter layout, App run/backup/download addresses, CRC logic |
| Keil after-build | App `project/2026706296.uvprojx` BIN output settings |

The old App-side header-BIN OTA is not the active contract. Do not reintroduce `Project_ota.bin`, `Function/uart_ota_app.c`, `Protocol/ota_image_protocol.c`, or `tools/pack_ota_image.exe` into the formal Keil build unless the contest protocol is intentionally replaced again.

### 2. Signatures

| Boundary | Signature / Entry | Contract |
|----------|-------------------|----------|
| Formal UART | USART1/RS485, `19200 8N1` | Contest communication and OTA commands must use this channel by default |
| App command | `0x0501` | App validates the frame, sends OK, writes Bootloader wait flags, and software-resets |
| Bootloader command | `0x0502` | Bootloader receives the contest bin raw stream after this command |
| Bootloader command | `0x0503` | Bootloader replies OK, then copies the received payload into the App run area |
| Contest bin magic | first 4 bytes little-endian `0x5AA5C33C` | Used only to identify the file; these bytes are not written to the App download area |
| App wait status | `updateStatus = 0x02` | Means "enter Bootloader and wait for contest OTA commands" |
| Copy status | `updateStatus = 0x01` | Means "download area contains a payload and Bootloader should copy it" |

### 3. Protocol Contract

All ordinary control frames are ASCII hexadecimal text. The binary frame encoded by that text uses:

| Field | Requirement |
|-------|-------------|
| Frame header | `0xA5B6`, big-endian |
| Frame tail | `0xB6A5`, big-endian |
| Protocol version | `0x02` |
| Command frame type | `0x01` |
| Response frame type | `0x02` |
| Error frame type | `0xFF` |
| OK payload | one byte `0xFF` |
| CRC | CRC-16-Modbus over the binary bytes from frame header through payload, transmitted big-endian |

`0x0502` is special: after the command frame is accepted, the PC sends raw bin bytes. The raw bin is not ASCII HEX-framed. Bootloader receives the raw bytes, validates the first four magic bytes, then writes only the following App payload to `0x08051000`.

### 4. Flash Layout

| Region | Address / Size | Owner | Contract |
|--------|----------------|-------|----------|
| Bootloader | `0x08000000 ~ 0x0800FFFF`, `64KB` | Bootloader image | MCU reset runs here first |
| Parameter area | `0x08010000 ~ 0x08010FFF`, `4KB` | App writes, Bootloader reads/writes | Holds update flags, size, CRC, version, and app start address |
| App run area | `0x08011000 ~ 0x08030FFF`, `128KB` | Bootloader writes final App | Must match App Keil IROM start and size |
| App backup area | `0x08031000 ~ 0x08050FFF`, `128KB` | Bootloader writes before updating | Holds previous run-area image for rollback |
| App download buffer | `0x08051000 ~ 0x08070FFF`, `128KB` | Bootloader writes received payload | Stores the magic-stripped App payload |
| Unused Flash area | `0x08071000 ~ 0x0807FFFF`, `60KB` | Unused | Keep unused unless the partition contract is redesigned |

### 5. Boot Parameter Fields

When App receives `0x0501`, it must only request the Bootloader waiting window:

| Field | Required Value |
|-------|----------------|
| `magicWord` | `0xC0DEF47A` |
| `updateFlag` | `0x5A` |
| `updateStatus` | `0x02` |

After Bootloader receives a valid bin after `0x0502`, it records the payload size and CRC32 in RAM/parameter data before executing the `0x0503` copy path. The copy path then uses the existing backup/copy/CRC/rollback logic with `updateStatus=0x01`.

### 6. Validation & Error Matrix

| Check | Valid Condition | Failure Result | Required Behavior |
|-------|-----------------|----------------|-------------------|
| Default baud | `19200` | Auto evaluation cannot communicate | Fix driver defaults and docs before testing protocol |
| App `0x0501` payload | zero length | Invalid command | Send error frame; do not reset |
| App wait flag write | parameter area write succeeds | Bootloader would not know to wait | Send error frame and stay in App |
| Bootloader wait entry | `magicWord` valid, `updateFlag=0x5A`, `updateStatus=0x02` | Ordinary boot | Stay silent, wait 5s, then jump App |
| `0x0502` frame | command frame valid and zero payload | Invalid prepare command | Send error frame |
| Bin magic | first four raw bytes equal `5AA5C33C` | Wrong or corrupt file | Send `0x0502` error, do not execute copy |
| Payload size | `1..128KB` after stripping magic | Too small or too large | Send error, do not copy |
| `0x0503` before bin | bin not received successfully | No image to copy | Send `0x0503` error |
| Backup before update | run area backup verifies | Cannot rollback reliably | Skip new copy and preserve current App |
| New App copy | run-area CRC32 matches received payload CRC32 | Copy/writeback failed | Restore backup when possible |
| Success | copy and CRC pass | Ready for new App | Clear flags, update counters, reset |

### 7. Good / Base / Bad Cases

| Case | Input | Expected Result |
|------|-------|-----------------|
| Good | `0x0501`, reset, `0x0502`, valid bin with `5AA5C33C`, `0x0503` | App replies OK; Bootloader receives payload, replies OK, copies, verifies, and restarts |
| Base | Normal power-on without `0x0501` | Bootloader stays silent for 5s and jumps existing App |
| Base | App receives unknown command | App sends error frame and does not alter Bootloader flags |
| Bad | Sending old `Project_ota.bin` to App | App should not have an OTA raw receiver; data is not a valid contest command frame |
| Bad | Bootloader receives a bin without `5AA5C33C` | Bootloader sends error and does not set copy status |
| Bad | Any docs still say `115200` or `Project_ota.bin` for formal OTA | Operator workflow is stale; update docs and specs |

### 8. Tests Required

Before committing OTA-related changes, run:

```powershell
& 'E:\Keil_v5\UV4\UV4.exe' -b 'project\2026706296.uvprojx' -j0
Select-String -Path 'project\output\Project.build_log.htm' -Pattern 'Program Size|Error\(s\)|Warning\(s\)'
Test-Path 'project\output\Project.bin'

Push-Location 'D:\GD32\2026706296_bootloader'
& 'E:\Keil_v5\UV4\UV4.exe' -b 'project\2026706296.uvprojx' -j0
Select-String -Path 'project\Objects\2026706296.build_log.htm' -Pattern 'Program Size|Error\(s\)|Warning\(s\)'
Pop-Location
```

Required assertions:

| Assertion | Expected Evidence |
|-----------|-------------------|
| App build | App build log reports `0 Error(s), 0 Warning(s)` |
| Bootloader build | Bootloader build log reports `0 Error(s), 0 Warning(s)` |
| Raw App output | `project/output/Project.bin` exists and is non-empty |
| No old formal OTA | App `*.uvprojx` does not compile `uart_ota_app.c` or `ota_image_protocol.c` |
| No old packer | App `AfterMake` does not run `pack_ota_image.exe` |
| Baud contract | App and Bootloader USART1/RS485 defaults are `19200` |

### 9. Operator Procedure

| Step | Command / Action | Required Evidence |
|------|------------------|-------------------|
| 1 | Build and flash the App at `0x08011000` and Bootloader at `0x08000000` | Both images build with no errors |
| 2 | Open RS485/USART1 at `19200 8N1` | Device responds to normal contest commands |
| 3 | Send `0x0501` to App | App replies OK and resets |
| 4 | Within the Bootloader 10s window, send `0x0502` | Bootloader accepts prepare command |
| 5 | Send the contest bin raw bytes, beginning with `5AA5C33C` | Bootloader receives and writes payload |
| 6 | Send `0x0503` | Bootloader replies OK and performs copy/verify/reset |

### 10. Wrong vs Correct

#### Wrong

```text
Raw/direct file send -> project/output/Project_ota.bin at 115200
```

#### Correct

```text
0x0501 -> reset to Bootloader -> 0x0502 -> contest bin with 5AA5C33C -> 0x0503 at 19200
```

#### Wrong

```xml
<RunUserProg2>1</RunUserProg2>
<UserProg2Name>..\tools\pack_ota_image.exe .\output\Project.bin .\output\Project_ota.bin 0x00000001 0x08011000</UserProg2Name>
```

#### Correct

```xml
<RunUserProg2>0</RunUserProg2>
<UserProg2Name></UserProg2Name>
```

---

## Scenario: Bootloader Handoff Standalone Hang

Use this scenario whenever the device boots through Bootloader and does not reach App startup after the handoff.

| Boundary | Contract |
|----------|----------|
| App vector table | App image starts at `0x08011000`, first word is SRAM MSP, second word is Thumb Reset_Handler inside App flash |
| Bootloader cleanup | Disable SysTick, clear pending interrupts, set `SCB->VTOR`, set MSP, then branch to App Reset_Handler |
| App runtime | ARMCLANG builds must provide `__use_no_semihosting` |
| Retarget stubs | `User/main.c` owns `_sys_open`, `_sys_write`, `_sys_read`, `_sys_exit`, `_ttywrch`, and `fputc` |
| Early output | Formal firmware drops `printf()` output by default so USART1/RS485 is not polluted |

Validation:

```powershell
Select-String -Path 'project\Listings\Project.map' -Pattern '__use_no_semihosting|_sys_open|_sys_write|_sys_exit|_ttywrch'
```

If a debugger stops at `BKPT 0xAB` before `main()`, treat it as semihosting leakage, not a Bootloader address problem.
