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

### Do not change user-facing firmware workflows without syncing repository docs

When you change a user-visible workflow or default value, code changes alone are not enough.
Firmware projects in this repository depend on Markdown documents for flashing, OTA, serial
tooling, and validation steps. Leaving old commands or stale defaults in docs is treated as
a quality bug, not just a documentation gap.

#### 1. Scope / Trigger

- Trigger: changing UART baud rates, flash addresses, partition sizes, upgrade commands, CLI arguments, expected log output, or hardware-operation steps.
- Trigger: changing BootLoader/App interaction, OTA packet flow, wakeup procedure, or any action the operator must perform manually.

#### 2. Signatures

Repository documents that must be checked when user-visible workflow changes:

| Document Type | Typical Paths |
|---------------|---------------|
| Project prompt / working rules | `AGENTS.md` |
| User operation docs | repository root `*.md` such as `工程文档.md`, `BootLoader_APP_接入说明.md` |
| Code-spec docs | `.trellis/spec/backend/*.md`, `.trellis/spec/guides/*.md` when the change becomes a durable rule |

#### 3. Contracts

- If code changes modify a command, default baud rate, address, partition, or expected log line, update every affected repository document in the same task.
- If the change affects future implementation behavior or review expectations, also update the relevant `.trellis/spec/` file.
- Example commands in docs must match the current tool contract, including required flags such as `--baudrate`.
- If a document intentionally describes an original upstream example instead of the current local fork, label that distinction explicitly.

#### 4. Validation & Error Matrix

| Observation | Meaning | Required Action |
|-------------|---------|-----------------|
| Code uses new baud rate but docs still show old baud rate | Operator will follow stale procedure | Update all matching docs before commit |
| Tool prints new progress lines but docs still say "no response" | Troubleshooting guidance is outdated | Add current example output |
| Local fork differs from upstream reference doc | Reader may confuse original behavior with current project behavior | Mark "upstream/original" vs "current repo copy" explicitly |
| Only `AGENTS.md` changed but user docs did not | Rule exists, but operator workflow is still stale | Update repository-facing Markdown docs too |

#### 5. Good / Base / Bad Cases

| Case | Expected Result |
|------|-----------------|
| Good | Code, `AGENTS.md`, repository docs, and `.trellis/spec/` all show the same baud rate, commands, and workflow |
| Base | Internal code-only refactor with no user-visible behavior change; repository docs may remain unchanged |
| Bad | OTA tool default changes to `460800`, but docs still instruct users to send at `115200` |

#### 6. Tests Required

- Search the repo for the old value or old command before commit.
- Run `git diff --check` on changed docs.
- Re-read at least one command example and one expected output example from the changed docs against the current implementation.

#### 7. Wrong vs Correct

##### Wrong

```text
Code changed:
- UART OTA default baudrate = 460800

Docs still say:
- python tools\make_uart_ota_packet.py --mode send --port COM7 --version 0x00000002 --chunk-size 512
- 波特率: 115200
```

##### Correct

```text
Code changed:
- UART OTA default baudrate = 460800

Docs updated in same task:
- send command includes --baudrate 460800
- troubleshooting/output examples reflect current ACK progress logs
- AGENTS/spec rules mention mandatory doc sync
```

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

### Deep-sleep wakeup must preserve the App vector table and wake IRQ state

Deep-sleep recovery is a cross-layer contract between the PMU, GPIO/EXTI, NVIC, SysTick,
BootLoader App relocation, and board-level re-initialization. Do not treat a wake button
as only a GPIO problem.

#### 1. Scope / Trigger

- Trigger: editing `USER/Driver/bsp_power.c`, `USER/Driver/bsp_key.c`, `USER/gd32f4xx_it.c`, `USER/boot_app_config.c`, or any wakeup/low-power path.
- Trigger: changing the App start address, BootLoader handoff, vector-table setup, SysTick setup, or EXTI wake source.
- Project example: the App runs at `0x0800D000`, but `SystemInit()` resets `SCB->VTOR` to the default Flash base. If wake recovery does not switch VTOR back before interrupts resume, SysTick/EXTI/USART can dispatch through the BootLoader vector table and look like "wake button does not return".

#### 2. Signatures

Expected low-power entries and ownership:

```c
void bsp_wkup_key_exti_init(void);
void bsp_enter_deepsleep(void);
static void bsp_deepsleep_reinit_after_wakeup(void);
void boot_app_vector_table_init(void);
void EXTI0_IRQHandler(void);
```

Expected App relocation contract:

| Symbol / Function | Required Value / Behavior |
|-------------------|---------------------------|
| `BOOT_APP_START_ADDRESS` | App vector table base, currently `0x0800D000` |
| `boot_app_vector_table_init()` | Writes `SCB->VTOR = BOOT_APP_START_ADDRESS`, then executes `__DSB()` and `__ISB()` |
| `SystemInit()` | May restore clock tree and default `SCB->VTOR`; callers in relocated App code must correct VTOR immediately afterward |
| `bsp_wkup_key_exti_init()` | Configures PA0/WK_UP as EXTI0 wake source and clears stale EXTI/NVIC pending state |
| `EXTI0_IRQHandler()` | Clears EXTI0 interrupt flag only; heavy re-init stays in the WFI return path |

#### 3. Contracts

- WK_UP hardware polarity must be checked against the board schematic before choosing EXTI trigger edge.
- For the current board, WK_UP is externally pulled up and the button shorts to ground, so the wake trigger is `EXTI_TRIG_FALLING`.
- Clear both `exti_interrupt_flag_clear(EXTI_0)` and `NVIC_ClearPendingIRQ(EXTI0_IRQn)` before entering WFI so stale events are not consumed as the wake event.
- If `SystemInit()` is called after wake in a relocated App, wrap the clock/vector-table recovery in a short interrupt-disabled section.
- After `SystemInit()`, call `boot_app_vector_table_init()` before enabling interrupts or allowing SysTick/peripheral IRQs to run.
- Prefer `PMU_LOWDRIVER_DISABLE` while validating wake reliability. Re-enable low-driver mode only after hardware smoke tests prove the board wakes consistently.
- Keep `EXTI0_IRQHandler()` small: clear the flag and return. Do not rebuild clocks, storage, OLED, or serial drivers inside the ISR.

#### 4. Validation & Error Matrix

| Observation | Likely Meaning | First Check |
|-------------|----------------|-------------|
| KEY2 enters deep sleep, WK_UP press has no visible effect | EXTI0 did not wake, stale pending was consumed, wrong edge, or PMU low-driver issue | Check WK_UP polarity, EXTI trigger, and EXTI/NVIC pending clear |
| WK_UP wakes once but later interrupts or serial logs stop | VTOR may still point to BootLoader after `SystemInit()` | Check `boot_app_vector_table_init()` is called immediately after `SystemInit()` |
| Wake returns only with debugger attached | Timing or pending interrupt race is hiding the issue | Add LED/RAM markers before WFI and after WFI return |
| Wake returns but OLED/UART/ADC stay broken | Re-init order or disabled peripheral clock is incomplete | Compare `bsp_deepsleep_reinit_after_wakeup()` against `system_init()` dependency order |
| Wake works with normal LDO but fails with low-driver | Low-driver mode is not reliable for this board state | Keep `PMU_LOWDRIVER_DISABLE` until current measurements justify changing it |

#### 5. Good / Base / Bad Cases

| Case | Expected Result |
|------|-----------------|
| Good | KEY2 enters deep sleep; WK_UP falling edge wakes; App restores VTOR to `0x0800D000`; UART/OLED/tasks resume |
| Base | Normal reset still prints BootLoader/App logs and App starts at `0x0800D000` |
| Bad | Calling `SystemInit()` after wake and leaving VTOR at `0x08000000` |
| Bad | Using both-edge wake for a pulled-up button and letting release generate an unnecessary EXTI0 interrupt |
| Bad | Clearing only EXTI flag but not NVIC pending before WFI |

#### 6. Tests Required

- Build the App and confirm Keil reports `0 Error(s)`.
- Upgrade or flash the App, then reset and confirm the boot log reports the expected `appVersion`.
- Press KEY2 and confirm the board enters the intended low-power state.
- Press WK_UP and confirm the App visibly returns: debug UART logs resume, OLED/tasks recover, and the board remains responsive.
- If wake still fails, add one-shot markers in this order: before WFI, inside `EXTI0_IRQHandler()`, immediately after WFI return, after `boot_app_vector_table_init()`, and after peripheral re-init.

#### 7. Wrong vs Correct

##### Wrong

```c
/* Wrong: SystemInit() can point VTOR back to 0x08000000 in a BootLoader App. */
SystemInit();
SystemCoreClockUpdate();
systick_config();
```

##### Correct

```c
/* Correct: keep interrupts closed while the relocated App restores clock and VTOR. */
__disable_irq();
SystemInit();
boot_app_vector_table_init();
SystemCoreClockUpdate();
systick_config();
__enable_irq();
```

##### Wrong

```c
/* Wrong for the current board: release edge adds a second wake interrupt. */
exti_init(EXTI_0, EXTI_INTERRUPT, EXTI_TRIG_BOTH);
```

##### Correct

```c
/* Correct for the current WK_UP circuit: external pull-up, press shorts to ground. */
exti_interrupt_flag_clear(EXTI_0);
NVIC_ClearPendingIRQ(EXTI0_IRQn);
exti_init(EXTI_0, EXTI_INTERRUPT, EXTI_TRIG_FALLING);
```

Additional runtime contract:

- `EXTI0` is a deep-sleep wake source, not a permanent application interrupt.
- After wake recovery completes and normal app peripherals are restored, explicitly disable `EXTI0_IRQn`, disable `EXTI_0`, and clear both EXTI/NVIC pending state.
- This prevents the low-power-only wake path from leaking into normal runtime button handling.

### RTC backup-domain restore must preserve VBAT-backed time

If the board provides a battery-backed `VBAT` rail, the RTC should continue running across main-power loss.
Do not overwrite RTC date/time on every boot just because the firmware re-entered `bsp_rtc_init()`.

#### 1. Scope / Trigger

- Trigger: editing `USER/Driver/bsp_rtc.c`, `USER/Driver/bsp_rtc.h`, board power notes, or any startup path that calls `bsp_rtc_init()`.
- Trigger: changing RTC clock-source setup, backup-register markers, default date/time values, or `VBAT` wiring assumptions.

#### 2. Signatures

Expected resources and behavior:

```c
#define BKP_VALUE 0x32F0U

int bsp_rtc_init(void);
static uint8_t bsp_rtc_has_valid_backup(void);
static int bsp_rtc_restore_from_backup(void);
```

Backup-domain contract:

| Item | Required Behavior |
|------|-------------------|
| `RTC_BKP0` | Stores a project-owned marker proving the RTC backup domain was initialized before |
| Valid backup marker | Reuse current RTC time/date; do not rewrite defaults |
| Missing backup marker | Write project default time/date and then store the marker |
| `VBAT` battery present | RTC should continue counting when main 3V3 is removed |

#### 3. Contracts

- Read the backup marker before deciding whether this boot is a restore path or a cold RTC initialization.
- If backup state is valid, do not call `rtc_init()` with default date/time values.
- If backup state is valid, do not blindly rewrite `RCU_BDCTL_RTCSRC`; preserve the running RTC clock source unless the backup domain is intentionally reset.
- On restore path, call `rtc_register_sync_wait()` and then `rtc_current_time_get()` so shared runtime structures reflect the persisted RTC registers.
- Only write default date/time on first initialization or after the backup domain is known to be invalid.

#### 4. Validation & Error Matrix

| Observation | Likely Meaning | Required Action |
|-------------|----------------|-----------------|
| RTC resets to default after every wake or reboot | Startup path is rewriting RTC unconditionally | Split cold-init and backup-restore paths |
| RTC survives reset but not full power loss with battery installed | Backup marker or RTC source may be reconfigured incorrectly | Check `RTC_BKP0`, `VBAT` wiring, and `RTCSRC` rewrite behavior |
| RTC time reads garbage after restore | Shadow registers were not resynced | Call `rtc_register_sync_wait()` before `rtc_current_time_get()` |
| No battery installed, RTC falls back to default time | Expected cold-start behavior | Document this as normal |

#### 5. Good / Base / Bad Cases

| Case | Expected Result |
|------|-----------------|
| Good | `VBAT` battery installed, main power removed then restored, RTC continues from previous time |
| Base | No `VBAT` battery installed, RTC reverts to default startup time after full power loss |
| Bad | `bsp_rtc_init()` always calls `rtc_init()` and rewrites time even when backup domain is valid |

#### 6. Tests Required

- Power the board from main `3V3`, set/observe a non-default RTC time, then remove only main power while keeping `VBAT` present; after restore, confirm the time advanced instead of resetting.
- Repeat with no `VBAT` battery installed; confirm the board falls back to the documented default startup time.
- Confirm deep-sleep wake still keeps RTC readable after `bsp_rtc_init()` is re-entered.

#### 7. Wrong vs Correct

##### Wrong

```c
/* Wrong: overwrites persisted RTC time on every boot. */
bsp_rtc_pre_cfg();
rtc_init(&rtc_initpara);
RTC_BKP0 = BKP_VALUE;
```

##### Correct

```c
/* Correct: only cold-start writes defaults; VBAT-backed restore just resyncs and reads current time. */
if (RTC_BKP0 == BKP_VALUE) {
    rtc_register_sync_wait();
    rtc_current_time_get(&rtc_initpara);
} else {
    rtc_init(&rtc_initpara);
    RTC_BKP0 = BKP_VALUE;
}
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
