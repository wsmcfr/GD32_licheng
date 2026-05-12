# Quality Guidelines

> Code quality standards for low-level firmware work in this project.

---

## Overview

This repository does not currently have automated linting or unit-test infrastructure for the firmware C code.
Quality is maintained through:

- strong module boundaries
- explicit initialization and ownership
- defensive checks around buffers and return codes
- hardware-backed smoke tests
- readable comments and stable naming

---

## Forbidden Patterns

### Do not put heavy work in interrupt handlers

Bad patterns include:

- formatting long strings
- scanning storage
- doing business logic in the ISR

Reference: `USER/gd32f4xx_it.c` keeps ISR work limited to flag clear, bounded copy, DMA re-arm, and exit.

### Do not change hardware constants without impact search

Before changing:

- pin macros
- DMA channel mappings
- baud rates
- buffer sizes

search the repository for all dependent usages.

### RS485 direction-control pins must be verified against the physical board

RS485 receive success does not prove that the direction-control GPIO is correct.
For half-duplex transceivers such as MAX3485, the MCU can receive through `RO -> USART_RX`
even when the software direction pin is mapped to the wrong GPIO. Always verify the
`DE/RE#` control net against the schematic and the physical board before debugging
application-layer forwarding.

#### 1. Scope / Trigger

- Trigger: adding or debugging an RS485 half-duplex interface, changing UART pin macros, or changing a transceiver direction-control GPIO.
- Example from this project: the RS485 direction-control pin was initially treated as `PA1`, but the board flow-control net was actually `PE8`. RX still worked, which made the wrong direction pin look plausible until TX failed.

#### 2. Signatures

Expected driver-level resources belong in `USER/Driver/bsp_usart.h` or the owning driver header:

```c
#define RS485_USART                    USART1
#define RS485_DIR_PORT                 GPIOE
#define RS485_DIR_CLK_PORT             RCU_GPIOE
#define RS485_DIR_PIN                  GPIO_PIN_8
#define RS485_DIR_TX_LEVEL             SET
#define RS485_DIR_RX_LEVEL             RESET

void bsp_rs485_direction_receive(void);
void bsp_rs485_direction_transmit(void);
```

For MAX3485-style wiring where `DE` and `RE#` share one MCU GPIO:

| GPIO Level | DE | RE# | Transceiver State |
|------------|----|-----|-------------------|
| `RESET` | disabled | enabled | Receive |
| `SET` | enabled | disabled | Transmit |

If the board uses an inverter or a different transceiver, document the polarity next to the macros and verify it on the actual pins.

#### 3. Contracts

- The direction GPIO macro must name the physical net connected to the transceiver `DE/RE#`, not a guessed or stale schematic label.
- Direction GPIO initialization must enable the correct GPIO port clock before configuring the pin as push-pull output.
- `bsp_rs485_direction_transmit()` must set the transceiver into transmit state before writing USART bytes.
- `bsp_rs485_direction_receive()` must return the transceiver to receive state after `USART_FLAG_TC` confirms the final byte has shifted out.
- Application-layer TX success logs, such as `sent == len` and `USART_FLAG_TC == SET`, only prove MCU USART completion; they do not prove the RS485 transceiver drove A/B.

#### 4. Validation & Error Matrix

| Observation | Meaning | Next Check |
|-------------|---------|------------|
| RS485 peer sends data and MCU receives it | `RO -> USART_RX` works | Still verify `DE/RE#` direction GPIO separately |
| MCU `USART_TX` pin has waveform but RS485 peer receives nothing | UART TX works, transceiver may not be in transmit mode | Probe `DE/RE#` on the transceiver pin |
| Direction GPIO output register changes but transceiver `DE/RE#` pin does not | wrong GPIO macro, broken net, solder issue, or measuring wrong node | Continuity-test MCU pin to transceiver pins with power off |
| Changing TX/RX direction polarity does not affect receive behavior | receive path is independent of the guessed control GPIO | Re-check physical direction-control pin mapping |
| Direction pin is a sine/noisy waveform when idle | likely floating, wrong probe ground, wrong node, or un-driven direction net | Probe MCU pin and transceiver pin directly with DC coupling |

#### 5. Good / Base / Bad Cases

| Case | Expected Result |
|------|-----------------|
| Good | `USART_TX` has waveform, `DE/RE#` follows the configured direction GPIO, A/B has differential waveform, peer receives bytes |
| Base | Peer-to-MCU receive works, proving `RO -> USART_RX`; this is necessary but not sufficient for MCU-to-peer TX |
| Bad | Peer-to-MCU receive works but MCU-to-peer TX fails because the direction macro points to an unrelated GPIO such as `PA1` instead of the board's `PE8` |

#### 6. Tests Required

- Power-off continuity test: verify the configured MCU GPIO pin is electrically connected to transceiver `DE` and `RE#`.
- Scope test: in transmit, probe the MCU direction pin, transceiver `DE/RE#`, `USART_TX`, and RS485 A/B.
- Functional test: send bytes from the debug UART through RS485 and assert the peer receives exactly those bytes.
- Regression check: after changing RS485 pin macros, run a receive test and a transmit test; do not accept receive-only success as proof.

#### 7. Wrong vs Correct

##### Wrong

```c
/* Wrong: guessed direction GPIO. RX can still work, hiding the mistake. */
#define RS485_DIR_PORT                 GPIOA
#define RS485_DIR_CLK_PORT             RCU_GPIOA
#define RS485_DIR_PIN                  GPIO_PIN_1
```

##### Correct

```c
/* Correct: use the board-verified flow-control net connected to MAX3485 DE/RE#. */
#define RS485_DIR_PORT                 GPIOE
#define RS485_DIR_CLK_PORT             RCU_GPIOE
#define RS485_DIR_PIN                  GPIO_PIN_8
```

### Do not use dynamic allocation in low-level storage or hot runtime paths

The existing storage port uses static buffers in `USER/Component/gd25qxx/lfs_port.c`.
Follow that pattern unless there is a very strong reason to introduce heap use.

### Do not expose private helpers through headers

Keep callback glue and local helpers `static` in the `.c` file.
Examples:

- `memory_compare()` in `USER/App/sd_app.c`
- `bsp_usart_disable_for_deepsleep()` in `USER/Driver/bsp_power.c`

### Do not ignore valid-length tracking

Always respect the actual data length instead of a whole fixed buffer.
This matters for both UART frames and file writes.

### Keil after-build helper commands must not fail the main target

When a Keil `AfterMake` command only generates a secondary artifact such as `Project.bin`,
do not set `nStopA1X` to `1`. A non-zero return code from the helper can make Keil report
`Target not created` even when linking, HEX generation, and the main AXF are valid.

#### 1. Scope / Trigger

- Trigger: editing `*.uvprojx` `AfterMake` settings, adding `fromelf --bin`, changing generated BIN/HEX outputs, or diagnosing a Keil log where `Program Size` is printed before `Target not created`.
- Project example: `MDK/2026706296.uvprojx` runs `fromelf.exe --bin --output=.\output\Project.bin .\output\Project.axf` after linking. The helper should not be allowed to convert a valid AXF build into a failed target.

#### 2. Signatures

Expected `AfterMake` XML contract for BIN generation:

```xml
<AfterMake>
  <RunUserProg1>1</RunUserProg1>
  <RunUserProg2>0</RunUserProg2>
  <UserProg1Name>E:\Keil_v5\ARM\ARMCLANG\bin\fromelf.exe --bin --output=.\output\Project.bin .\output\Project.axf</UserProg1Name>
  <UserProg2Name></UserProg2Name>
  <UserProg1Dos16Mode>0</UserProg1Dos16Mode>
  <UserProg2Dos16Mode>0</UserProg2Dos16Mode>
  <nStopA1X>0</nStopA1X>
  <nStopA2X>0</nStopA2X>
</AfterMake>
```

Required build artifacts for the App target:

| Artifact | Contract |
|----------|----------|
| `MDK/output/Project.axf` | Main link output; must exist after a successful target build |
| `MDK/output/Project.hex` | Addressed image generated by Keil HEX output |
| `MDK/output/Project.bin` | Secondary raw image generated by `fromelf`; must be non-empty before using BootLoader upgrade flow |

#### 3. Contracts

- `RunUserProg1` may be `1` when BIN generation is needed.
- `UserProg1Name` should call `fromelf.exe` directly unless a script is truly required.
- `nStopA1X` must stay `0` for helper-only artifact generation.
- A `Target not created` log after successful linking must first be checked against `AfterMake`, especially `nStopA1X`.
- A successful Keil target is not enough to prove BIN generation; always verify `Project.bin` exists and has non-zero size.

#### 4. Validation & Error Matrix

| Observation | Likely Meaning | First Check |
|-------------|----------------|-------------|
| `Program Size` is printed, then `After Build - User command #1`, then `Target not created` | Main link likely succeeded, helper command affected target status | Check `<nStopA1X>` and helper command path |
| Build log reports `".\output\Project.axf" - 1 Error(s), 0 Warning(s)` after an after-build command | Keil may be attributing a user-command failure to the AXF target | Inspect `AfterMake`, not only C source |
| `Project.bin` exists and is non-empty but Keil still reports failure | Helper may have returned non-zero while producing output | Keep `nStopA1X=0` for helper-only commands |
| `Project.hex` exists but `Project.bin` is missing | Main target may be valid, BIN helper failed | Verify `fromelf.exe` path and output path |
| Changing C code does not affect the failure | Failure is likely project configuration or after-build tooling | Compare `*.uvprojx` with a reference project |

#### 5. Good / Base / Bad Cases

| Case | Expected Result |
|------|-----------------|
| Good | `RunUserProg1=1`, `UserProg1Name` calls `fromelf`, `nStopA1X=0`, build log ends with `0 Error(s), 0 Warning(s)`, and `Project.bin` is non-empty |
| Base | `RunUserProg1=0`; main target builds and HEX is generated, but no automatic BIN is produced |
| Bad | `RunUserProg1=1` and `nStopA1X=1`; a helper return-code issue makes Keil report `Target not created` even after the main target linked |

#### 6. Tests Required

- Rebuild in Keil and confirm the build log ends with `0 Error(s), 0 Warning(s)`.
- Confirm `MDK/output/Project.bin` exists and its file size is greater than zero.
- If `Target not created` appears after `After Build - User command #1`, inspect `MDK/2026706296.uvprojx` before changing firmware code.
- When changing the BIN command, compare the `AfterMake` block against a known-good reference where `nStopA1X=0`.

#### 7. Wrong vs Correct

##### Wrong

```xml
<!-- Wrong: helper command return code can fail the whole Keil target. -->
<AfterMake>
  <RunUserProg1>1</RunUserProg1>
  <UserProg1Name>E:\Keil_v5\ARM\ARMCLANG\bin\fromelf.exe --bin --output=.\output\Project.bin .\output\Project.axf</UserProg1Name>
  <nStopA1X>1</nStopA1X>
</AfterMake>
```

##### Correct

```xml
<!-- Correct: BIN generation is a helper step; verify Project.bin separately. -->
<AfterMake>
  <RunUserProg1>1</RunUserProg1>
  <UserProg1Name>E:\Keil_v5\ARM\ARMCLANG\bin\fromelf.exe --bin --output=.\output\Project.bin .\output\Project.axf</UserProg1Name>
  <nStopA1X>0</nStopA1X>
</AfterMake>
```

---

## Required Patterns

### One public header per module

Each module should have:

- a `.h` with macros, `extern` declarations, and public function prototypes
- a `.c` with private helpers and implementation details

### Explicit initialization order

System bring-up is intentionally serialized in `USER/App/scheduler.c::system_init()`.
If a new peripheral has dependencies, place it in the correct boot order and log the stage if useful.

### Fixed-width integer types for hardware-facing data

Use `uint8_t`, `uint16_t`, `uint32_t`, and explicit buffer-size macros for data that crosses registers, DMA, or storage boundaries.

### Bounded buffer operations

Good existing examples:

- `vsnprintf(buffer, sizeof(buffer), ...)`
- UART copy length clamp in `USART0_IRQHandler()`
- file readback length check in `sd_fatfs_test()`

### Backward-compatible wrappers when renaming public interfaces

The `sd_lfs_*` wrappers in `USER/App/sd_app.c` show the expected compatibility style when an external name still exists in call sites or old documentation.

---

## Testing Requirements

For backend changes, prefer hardware-backed smoke tests over unverified assumptions.

Minimum expected checks depend on the changed area:

| Area | Minimum Validation |
|------|--------------------|
| UART / DMA / IRQ | boot log still prints, receive path still echoes or frames correctly |
| Storage | init, open/write/read, and readback verification still pass |
| OLED / I2C / SPI | boot sequence completes and dependent app task still runs |
| Power / wakeup | enter sleep, wake with key, and re-init order still works |
| ADC / DAC / RTC | displayed values or serial diagnostics still update |

Use the existing self-test paths where possible:

- `test_spi_flash()`
- `sd_fatfs_test()`
- boot logs in `system_init()`

---

## Code Review Checklist

- Is the file placed in the correct layer: `Driver`, `Component`, `App`, or infrastructure?
- Are pin macros, DMA resources, and shared buffers declared in the header that owns them?
- Are private helpers `static`?
- Are return codes checked immediately?
- Are copy lengths and formatted buffers bounded?
- Does any ISR stay minimal and defer work to app context?
- If an API name changed, is backward compatibility needed?
- Is the new behavior covered by a concrete hardware or smoke-test plan?
