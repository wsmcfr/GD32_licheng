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

Reference: `User/gd32f4xx_it.c` keeps ISR work limited to flag clear, bounded copy, DMA re-arm, and exit.

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
- send `project/output/Project.bin` through a removed packet sender
- 波特率: 115200
```

##### Correct

```text
Code changed:
- UART OTA default baudrate = 460800

Docs updated in same task:
- operation steps say to raw/direct-send `project/output/Project_ota.bin` at `460800`
- troubleshooting/output examples reflect current `OTA: header ok`, `OTA: payload ok`, and BootLoader logs
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

Expected driver-level resources belong in `Driver/USART/bsp_usart.h` or the owning driver header:

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

The current GD25Q16 SMARTFS port uses static metadata and sector buffers in
`Driver/GD25QXX/smartfs_port.c`. Follow that pattern unless there is a
very strong reason to introduce heap use.

### OLED I2C display writes should use page-sized batch transfers

#### 1. Scope / Trigger

- Trigger: modifying `Driver/OLED/oled.c`, `Driver/OLED/oled.h`, `Driver/OLED/bsp_oled.c`, or `Driver/OLED/bsp_oled.h`.
- Trigger: changing SSD1306 clear, fill, bitmap, character, or string rendering paths.
- Trigger: changing OLED I2C0 DMA buffer sizes or public OLED write APIs.

#### 2. Signatures

Expected BSP buffer contract:

```c
#define OLED_TX_DATA_MAX_SIZE          128U
#define OLED_TX_BUFFER_SIZE            (OLED_TX_DATA_MAX_SIZE + 1U)

extern __IO uint8_t oled_cmd_buf[2];
extern __IO uint8_t oled_data_buf[OLED_TX_BUFFER_SIZE];
```

Expected component API:

```c
uint8_t OLED_Write_cmd(uint8_t cmd);
uint8_t OLED_Write_cmd_buf(const uint8_t *cmds, uint16_t length);
uint8_t OLED_Write_data(uint8_t data);
uint8_t OLED_Write_data_buf(const uint8_t *data, uint16_t length);
uint8_t OLED_Set_Position(uint8_t x, uint8_t y);
uint8_t OLED_ShowStr(uint8_t x, uint8_t y, char *ch, uint8_t fontsize);
```

#### 3. Contracts

- `oled_data_buf[0]` is the SSD1306 I2C control byte `0x40`; the remaining bytes carry display data.
- `OLED_Write_data_buf()` must split writes larger than `OLED_TX_DATA_MAX_SIZE` into multiple DMA transfers.
- `OLED_Write_cmd_buf()` must batch consecutive SSD1306 commands and may reuse the DMA data buffer with control byte `0x00`.
- `OLED_Write_data()` remains a compatibility wrapper for single-byte writes and should route through `OLED_Write_data_buf()`.
- `OLED_Write_cmd()` remains a compatibility wrapper for single-byte commands and should route through `OLED_Write_cmd_buf()`.
- OLED write/display helpers that return `uint8_t` must return `1U` only after the full I2C/DMA transaction succeeds. They must return `0U` for invalid parameters, unavailable OLED state, bus/address/DMA/BTC/STOP timeout, or out-of-range cursor positions.
- `OLED_Set_Position()` should send page, high-column, and low-column commands in one `OLED_Write_cmd_buf()` transaction.
- `OLED_Clear()` and `OLED_Allfill()` should write one full 128-byte page per transaction instead of issuing 128 single-byte transactions per page.
- `OLED_ShowStr()` should batch-render 6x8 strings into row buffers instead of calling `OLED_ShowChar()` for every character.
- For 6x8 text, keep the legacy 8-pixel character step by writing 6 glyph columns plus 2 blank columns per character.
- App-layer `oled_printf()` should compare its line cache and refresh only the changed character span when possible. It may update `g_oled_line_cache` only after `OLED_ShowStr()` reports success; failed OLED writes must keep the old cache so the next task cycle retries.
- The low-level packet helper must wait for DMA FTF and I2C BTC before STOP, so the final byte is shifted out before the bus is released.
- On bus, address, DMA, or STOP timeout, OLED transmission may set `s_oled_available = 0U`; `OLED_Init()` is responsible for re-enabling OLED attempts.
- OLED bus-busy waits must stay short enough for the cooperative scheduler. Do not reintroduce 10000ms-class busy waits around `I2C_FLAG_I2CBSY`.

#### 4. Validation & Error Matrix

| Observation | Meaning | Required Action |
|-------------|---------|-----------------|
| `OLED_Clear()` loops over 128 calls to `OLED_Write_data(0)` per page | regressed to per-byte I2C transactions | use a 128-byte zero buffer and `OLED_Write_data_buf()` |
| `OLED_Set_Position()` calls `OLED_Write_cmd()` three times | regressed to three command transactions per cursor move | send the three position commands with `OLED_Write_cmd_buf()` |
| `OLED_ShowStr()` calls `OLED_ShowChar()` in a character loop | string updates still pay per-character positioning overhead | render one row/page segment into a buffer and call `OLED_Write_data_buf()` |
| 6x8 batch text writes only 6 bytes per character | app diff refresh positions drift from the legacy 8-pixel grid | append two blank columns for each 6x8 glyph |
| `oled_printf()` refreshes a whole 16-character line after a one-character change | high-frequency status rows still do avoidable I2C work | compute start/end diff indexes and refresh only that span |
| `oled_printf()` updates the line cache after a failed `OLED_ShowStr()` | display cache lies about physical screen contents | update cache only when the low-level write path returns success |
| OLED I2C busy recovery waits seconds before returning | one bad display can starve all scheduler tasks | keep the busy wait bounded to a small recovery window and mark OLED unavailable on failure |
| `oled_data_buf` is only 2 bytes | batch API cannot carry a page | restore `OLED_TX_BUFFER_SIZE = OLED_TX_DATA_MAX_SIZE + 1U` |
| DMA FTF is checked but I2C BTC is not checked before STOP | last byte may still be shifting | wait for `I2C_FLAG_BTC` before `i2c_stop_on_bus()` |
| new display path writes dynamic heap buffers | avoidable heap use in hot display path | use static buffers, stack buffers, or existing font arrays |
| OLED task becomes slow after text changes | too many START/STOP transactions | check `OLED_ShowChar()` and bitmap paths for batch writes |

#### 5. Good / Base / Bad Cases

| Case | Expected Result |
|------|-----------------|
| Good | `OLED_Set_Position()` uses one command transaction, `OLED_ShowStr()` sends row/page segments, and `OLED_Clear()` sends 4 page data transfers plus page-position commands |
| Base | `OLED_Write_data()` still works for legacy single-byte callers |
| Bad | each byte of a character or clear page starts its own I2C transaction |

#### 6. Tests Required

- Run `python tools/test_static_optimizations.py` and confirm OLED batch-transfer assertions pass.
- Run a Keil rebuild and confirm `project/output/Project.build_log.htm` reports `0 Error(s), 0 Warning(s)`.
- Search for stale OLED documentation such as `oled_data_buf[2]`, OLED `10ms` task period, `oled_printf` using a 512-byte buffer, or OLED write APIs documented as `void` when the implementation returns status.
- Hardware smoke test after flashing: OLED initializes, clears, displays all four app lines, and still turns off before deep sleep.

#### 7. Wrong vs Correct

##### Wrong

```c
for (n = 0U; n < 128U; n++) {
    OLED_Write_data(0x00U);
}
```

##### Correct

```c
static const uint8_t zeros[OLED_TX_DATA_MAX_SIZE] = {0U};

OLED_Write_data_buf(zeros, OLED_TX_DATA_MAX_SIZE);
```

### Do not expose private helpers through headers

Keep callback glue and local helpers `static` in the `.c` file.
Examples:

- storage shell helpers in `Function/usart_app.c`
- `bsp_usart_disable_for_deepsleep()` in `Driver/POWER/bsp_power.c`

### Do not ignore valid-length tracking

Always respect the actual data length instead of a whole fixed buffer.
This matters for both UART frames and file writes.

### Keil after-build helper commands must be explicit

When Keil `AfterMake` commands generate secondary artifacts such as `Project.bin` and
`Project_ota.bin`, keep each helper in its own `UserProg` entry and keep stop-on-error
disabled for helper-only steps. Keil does not execute one `UserProg` through a shell, so
shell operators such as `&&` are treated as program arguments and can silently prevent the
second artifact from being generated.

#### 1. Scope / Trigger

- Trigger: editing `*.uvprojx` `AfterMake` settings, adding `fromelf --bin`, adding `pack_ota_image.exe`, changing generated BIN/HEX outputs, or diagnosing a Keil log where `Program Size` is printed before an after-build warning/error.
- Project example: `project/2026706296.uvprojx` runs `fromelf.exe --bin --output=.\output\Project.bin .\output\Project.axf`, then runs `..\tools\pack_ota_image.exe .\output\Project.bin .\output\Project_ota.bin 0x00000001 0x08011000`.

#### 2. Signatures

Expected `AfterMake` XML contract for raw BIN and header-BIN generation:

```xml
<AfterMake>
  <RunUserProg1>1</RunUserProg1>
  <RunUserProg2>1</RunUserProg2>
  <UserProg1Name>E:\Keil_v5\ARM\ARMCLANG\bin\fromelf.exe --bin --output=.\output\Project.bin .\output\Project.axf</UserProg1Name>
  <UserProg2Name>..\tools\pack_ota_image.exe .\output\Project.bin .\output\Project_ota.bin 0x00000001 0x08011000</UserProg2Name>
  <UserProg1Dos16Mode>0</UserProg1Dos16Mode>
  <UserProg2Dos16Mode>0</UserProg2Dos16Mode>
  <nStopA1X>0</nStopA1X>
  <nStopA2X>0</nStopA2X>
</AfterMake>
```

Required build artifacts for the App target:

| Artifact | Contract |
|----------|----------|
| `project/output/Project.axf` | Main link output; must exist after a successful target build |
| `project/output/Project.hex` | Addressed image generated by Keil HEX output |
| `project/output/Project.bin` | Secondary raw image generated by `fromelf`; must be non-empty before using BootLoader upgrade flow |
| `project/output/Project_ota.bin` | Secondary OTA image generated by `pack_ota_image.exe`; must be exactly 64 bytes larger than `Project.bin` |

#### 3. Contracts

- `RunUserProg1` may be `1` when BIN generation is needed.
- `RunUserProg2` must be `1` when `Project_ota.bin` generation is needed.
- `UserProg1Name` should call `fromelf.exe` directly unless a script is truly required.
- `UserProg2Name` should call `pack_ota_image.exe` directly; do not join it to `UserProg1Name` with shell operators.
- `nStopA1X` and `nStopA2X` must stay `0` for helper-only artifact generation.
- A `Target not created` log after successful linking must first be checked against `AfterMake`, especially `nStopA1X`.
- A successful Keil target is not enough to prove BIN generation; always verify `Project.bin` and `Project_ota.bin` exist and have the expected sizes.

#### 4. Validation & Error Matrix

| Observation | Likely Meaning | First Check |
|-------------|----------------|-------------|
| `Program Size` is printed, then `After Build - User command #1`, then `Target not created` | Main link likely succeeded, helper command affected target status | Check `<nStopA1X>` and helper command path |
| Build log reports `".\output\Project.axf" - 1 Error(s), 0 Warning(s)` after an after-build command | Keil may be attributing a user-command failure to the AXF target | Inspect `AfterMake`, not only C source |
| `Project.bin` exists and is non-empty but Keil still reports failure | Helper may have returned non-zero while producing output | Keep `nStopA1X=0` for helper-only commands |
| `Project.hex` exists but `Project.bin` is missing | Main target may be valid, BIN helper failed | Verify `fromelf.exe` path and output path |
| `Project.bin` exists but `Project_ota.bin` is missing and log shows `Q0466E` | Multiple helpers were joined in one `UserProg` command and Keil passed shell tokens to `fromelf` | Split `fromelf` and `pack_ota_image.exe` into `UserProg1` and `UserProg2` |
| Changing C code does not affect the failure | Failure is likely project configuration or after-build tooling | Compare `*.uvprojx` with a reference project |

#### 5. Good / Base / Bad Cases

| Case | Expected Result |
|------|-----------------|
| Good | `RunUserProg1=1`, `RunUserProg2=1`, `UserProg1Name` calls `fromelf`, `UserProg2Name` calls `pack_ota_image.exe`, both stop flags are `0`, and both BIN files are non-empty |
| Base | `RunUserProg1=0`; main target builds and HEX is generated, but no automatic BIN is produced |
| Bad | `RunUserProg1=1` and `nStopA1X=1`; a helper return-code issue makes Keil report `Target not created` even after the main target linked |
| Bad | `UserProg1Name` contains `&&` to chain `fromelf` and the packer; Keil passes it as arguments rather than running a shell pipeline |

#### 6. Tests Required

- Rebuild in Keil and confirm the build log ends with `0 Error(s), 0 Warning(s)`.
- Confirm `project/output/Project.bin` exists and its file size is greater than zero.
- Confirm `project/output/Project_ota.bin` exists and is exactly 64 bytes larger than `Project.bin`.
- If `Target not created` appears after `After Build - User command #1`, inspect `project/2026706296.uvprojx` before changing firmware code.
- When changing the BIN command, compare the `AfterMake` block against a known-good reference where `nStopA1X=0`, `nStopA2X=0`, and helper commands are not shell-chained.

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
  <RunUserProg2>1</RunUserProg2>
  <UserProg1Name>E:\Keil_v5\ARM\ARMCLANG\bin\fromelf.exe --bin --output=.\output\Project.bin .\output\Project.axf</UserProg1Name>
  <UserProg2Name>..\tools\pack_ota_image.exe .\output\Project.bin .\output\Project_ota.bin 0x00000001 0x08011000</UserProg2Name>
  <nStopA1X>0</nStopA1X>
  <nStopA2X>0</nStopA2X>
</AfterMake>
```

### Deep-sleep wakeup must preserve the App vector table and wake IRQ state

Deep-sleep recovery is a cross-layer contract between the PMU, GPIO/EXTI, NVIC, SysTick,
BootLoader App relocation, and board-level re-initialization. Do not treat a wake button
as only a GPIO problem.

#### 1. Scope / Trigger

- Trigger: editing `Driver/POWER/bsp_power.c`, `Driver/KEY/bsp_key.c`, `User/gd32f4xx_it.c`, `User/boot_app_config.c`, or any wakeup/low-power path.
- Trigger: changing the App start address, BootLoader handoff, vector-table setup, SysTick setup, or EXTI wake source.
- Project example: the App runs at `0x08011000`, but `SystemInit()` resets `SCB->VTOR` to the default Flash base. If wake recovery does not switch VTOR back before interrupts resume, SysTick/EXTI/USART can dispatch through the BootLoader vector table and look like "wake button does not return".

#### 2. Signatures

Expected low-power entries and ownership:

```c
void bsp_wkup_key_exti_init(void);
void bsp_enter_sleep(void);
void bsp_enter_deepsleep(void);
void bsp_enter_standby(void);
static void bsp_oled_preblank_for_standby(void);
static void bsp_standby_preblank_indicators(void);
static void bsp_wait_key4_release_before_standby(void);
static void bsp_deepsleep_reinit_after_wakeup(void);
void boot_app_vector_table_init(void);
void EXTI0_IRQHandler(void);
```

Expected App relocation contract:

| Symbol / Function | Required Value / Behavior |
|-------------------|---------------------------|
| `BOOT_APP_START_ADDRESS` | App vector table base, currently `0x08011000` |
| `boot_app_vector_table_init()` | Writes `SCB->VTOR = BOOT_APP_START_ADDRESS`, then executes `__DSB()` and `__ISB()` |
| `SystemInit()` | May restore clock tree and default `SCB->VTOR`; callers in relocated App code must correct VTOR immediately afterward |
| `bsp_wkup_key_exti_init()` | Configures PA0/WK_UP as EXTI0 wake source and clears stale EXTI/NVIC pending state |
| `bsp_enter_sleep()` | Masks non-wakeup runtime IRQs, uses EXTI0 as the wake source, stops SysTick before `pmu_to_sleepmode(WFI_CMD)`, then restores timebase, runtime IRQs, and scheduler baselines |
| `EXTI0_IRQHandler()` | Clears EXTI0 interrupt flag only; heavy re-init stays in the WFI return path |
| `bsp_enter_standby()` | Uses PA0 as the PMU WKUP source, not as an EXTI wake source; wake resumes through reset/startup |
| `bsp_oled_preblank_for_standby()` | Sends SSD1306 display-off/charge-pump-off before waiting for KEY4 confirmation; does not shut down I2C/DMA/GPIO |
| `bsp_standby_preblank_indicators()` | Turns off all LED indicators through the LED app state source before the KEY4 confirmation wait |
| `bsp_wait_key4_release_before_standby()` | Waits for KEY4/PA7 to be pressed low, then released high for at least 20 ms before continuing |

#### 3. Contracts

- WK_UP hardware polarity must be checked against the board schematic before choosing EXTI trigger edge.
- For the current board, WK_UP is externally pulled up and the button shorts to ground, so the wake trigger is `EXTI_TRIG_FALLING`.
- Sleep and Deep-sleep use `EXTI0` falling edge for KEYW wakeup and return to the interrupted call path after recovery.
- Sleep can be woken by any enabled interrupt, so the current KEYW-only Sleep contract must temporarily mask USART0 and USART1 NVIC interrupts before WFI, then restore them after wake.
- Standby uses the PMU WKUP function on the same PA0 pin, not the EXTI0 interrupt path. `bsp_enter_standby()` must blank the OLED and LEDs before waiting for KEY4 press-release confirmation; KEYW/PA0 remains the wake source only and must not be required to enter Standby.
- `bsp_enter_standby()` must call the standby confirmation helpers in this order: `bsp_oled_preblank_for_standby()`, `bsp_standby_preblank_indicators()`, `bsp_wait_key4_release_before_standby()`, then `__disable_irq()` and destructive peripheral shutdown. This keeps the UI dark during confirmation while preserving `delay_ms()` for KEY4 debounce.
- KEY3 must not toggle LED3 before entering Standby prepare. LED state belongs to the Standby entry path so the indicator policy stays centralized and the confirmation wait cannot leave a visible LED on.
- Runtime button actions and power-entry LED blanking must update LED state through `led_app_set()`, `led_app_toggle()`, `led_app_all_off()`, or `led_app_blank_for_sleep()`. Do not directly use `LEDx_TOGGLE` or `LEDx_OFF` from app/power policy code, because that bypasses `ucLed[]` and the LED refresh cache.
- After GPIO or LED hardware is reinitialized during wake recovery, call `led_app_reset_cache()` before scheduler tasks resume so the next `led_task()` force-writes all six LEDs.
- KEY4 is a confirmation input only before true Standby. It may remain normal LED4-toggle UI in runtime, but during `bsp_enter_standby()` it must be read as a GPIO input until the confirmation sequence completes.
- KEYW/PA0 must be treated as a wake source only for Standby. Do not wait for KEYW to enter Standby; that makes the wake key part of the sleep-entry workflow and creates confusing operator behavior.
- Startup code should enable `RCU_PMU`, check `PMU_FLAG_STANDBY` after the debug UART is available, emit a short boot log such as `BOOT: wake from standby`, then clear standby/wakeup flags before normal initialization continues.
- Clear both `exti_interrupt_flag_clear(EXTI_0)` and `NVIC_ClearPendingIRQ(EXTI0_IRQn)` before entering WFI so stale events are not consumed as the wake event.
- If `SystemInit()` is called after wake in a relocated App, wrap the clock/vector-table recovery in a short interrupt-disabled section.
- After `SystemInit()`, call `boot_app_vector_table_init()` before enabling interrupts or allowing SysTick/peripheral IRQs to run.
- `User/systick.c` owns the App timebase. `SysTick_Handler()` must call `systick_tick_inc()` directly; do not reintroduce an external SysTick wrapper in the active App target.
- Before stopping or reconfiguring SysTick for deep sleep, call `timebase_prepare_reconfiguration()` so the local timebase state and pending SysTick interrupt are cleaned consistently.
- After `SystemCoreClockUpdate()` on wake recovery, call `timebase_update_after_clock_change()` before enabling interrupts or running peripherals that depend on `delay_ms()`, `delay_us()`, or `get_system_ms()`.
- If RTC is available before sleep, capture a seconds timestamp before stopping SysTick and call `timebase_adjust_ms()` after wake with the RTC elapsed time. SysTick is runtime time; RTC is wall time. Cross-sleep timeouts and logs must not silently lose the sleep interval.
- After wake reinitialization finishes, call `scheduler_reset_runtime()` so scheduled tasks restart from the current tick instead of all becoming due in the first main-loop pass.
- Device low-power commands issued before deep sleep, such as SPI Flash deep power-down or external ADC standby, must use bounded waits and return status. The power path may continue shutting down buses after a device command fails, but it must not spin forever after debug UARTs have already been disabled.
- Split wake recovery into mandatory and lazy work. Vector table, clocks, timebase, wake IRQ cleanup, GPIO, debug UART, and currently required peripherals are mandatory; storage mount or media detection should be deferred until first use unless the caller explicitly needs it during wake.
- `get_system_us()` must remain monotonic across the SysTick overflow window by compensating for a pending SysTick exception when the millisecond counter has not been serviced yet.
- `delay_us()` should use a DWT cycle conversion rounded up to the next cycle boundary when the core clock is not an integer multiple of 1 MHz; microsecond delays must not underrun for the sake of nominal precision.
- Prefer `PMU_LOWDRIVER_DISABLE` while validating wake reliability. Re-enable low-driver mode only after hardware smoke tests prove the board wakes consistently.
- Keep `EXTI0_IRQHandler()` small: clear the flag and return. Do not rebuild clocks, storage, OLED, or serial drivers inside the ISR.
- If adding or changing button mappings, keep the user-facing low-power mapping explicit: KEY1 = Sleep, KEY2 = Deep-sleep, KEY3 = Standby prepare, KEY4 release = Standby confirmation, KEYW = wake source.

#### 4. Validation & Error Matrix

| Observation | Likely Meaning | First Check |
|-------------|----------------|-------------|
| KEY2 enters deep sleep, WK_UP press has no visible effect | EXTI0 did not wake, stale pending was consumed, wrong edge, or PMU low-driver issue | Check WK_UP polarity, EXTI trigger, and EXTI/NVIC pending clear |
| KEY1 Sleep wakes immediately without pressing WK_UP | SysTick or another interrupt was left enabled as a wake source | Confirm `timebase_prepare_reconfiguration()` runs before `pmu_to_sleepmode(WFI_CMD)` |
| KEY3 Standby exits immediately after entering | PA0 was already in the PMU WKUP active state or WKUP flag was stale | Confirm PMU wake flag clearing and verify KEYW/PA0 hardware polarity |
| KEY3 Standby waits for KEY4 but OLED or LED stays lit | Indicator blanking still happens after the KEY4 wait | Confirm `bsp_oled_preblank_for_standby()` and `bsp_standby_preblank_indicators()` run before `bsp_wait_key4_release_before_standby()` |
| KEY3 Standby wakes but code appears to continue after `pmu_to_standbymode()` | Debug option bytes or debugger hold prevented true Standby | Treat the post-Standby path as abnormal and force `NVIC_SystemReset()` |
| WK_UP wakes once but later interrupts or serial logs stop | VTOR may still point to BootLoader after `SystemInit()` | Check `boot_app_vector_table_init()` is called immediately after `SystemInit()` |
| Wake returns only with debugger attached | Timing or pending interrupt race is hiding the issue | Add LED/RAM markers before WFI and after WFI return |
| Wake returns but OLED/UART/ADC stay broken | Re-init order or disabled peripheral clock is incomplete | Compare `bsp_deepsleep_reinit_after_wakeup()` against `system_init()` dependency order |
| Wake works with normal LDO but fails with low-driver | Low-driver mode is not reliable for this board state | Keep `PMU_LOWDRIVER_DISABLE` until current measurements justify changing it |

#### 5. Good / Base / Bad Cases

| Case | Expected Result |
|------|-----------------|
| Good | KEY2 enters deep sleep; WK_UP falling edge wakes; App restores VTOR to `0x08011000`; UART/OLED/tasks resume |
| Good | KEY1 enters Sleep; WK_UP falling edge wakes; App resumes without full peripheral reinitialization and scheduler baselines are reset |
| Good | KEY3 blanks OLED/LED first, waits for KEY4 press-release confirmation, enters Standby, then KEYW/PA0 wakes and the App logs `BOOT: wake from standby` |
| Base | Normal reset still prints BootLoader/App logs and App starts at `0x08011000` |
| Bad | Calling `SystemInit()` after wake and leaving VTOR at `0x08000000` |
| Bad | Using both-edge wake for a pulled-up button and letting release generate an unnecessary EXTI0 interrupt |
| Bad | Clearing only EXTI flag but not NVIC pending before WFI |
| Bad | KEY3 toggles LED3, then waits for KEY4/KEYW, leaving an LED visibly on during Standby confirmation |
| Bad | Standby entry waits for KEYW/PA0, making the wake key part of the sleep-entry workflow |

#### 6. Tests Required

- Build the App and confirm Keil reports `0 Error(s)`.
- Upgrade or flash the App, then reset and confirm the boot log reports the expected `appVersion`.
- Press KEY2 and confirm the board enters the intended low-power state.
- Press WK_UP and confirm the App visibly returns: debug UART logs resume, OLED/tasks recover, and the board remains responsive.
- Press KEY1 and confirm Sleep pauses the CPU until WK_UP is pressed, then normal tasks resume.
- Press KEY3 and confirm the OLED and LEDs turn off before the KEY4 wait, press and release KEY4 to confirm Standby entry, then use KEYW/PA0 to wake and confirm the App restarts with `BOOT: wake from standby`.
- Run `python tools/test_static_optimizations.py` and confirm it asserts that `bsp_wait_keyw_low_before_standby` is absent, `bsp_wait_key4_release_before_standby()` is present, OLED/LED blanking precedes KEY4 wait, and KEY3 no longer toggles LED3.
- If wake still fails, add one-shot markers in this order: before WFI, inside `EXTI0_IRQHandler()`, immediately after WFI return, after `boot_app_vector_table_init()`, and after peripheral re-init.

#### 7. Wrong vs Correct

##### Wrong

```c
/* Wrong: KEYW is the wake source, so do not require it to enter Standby. */
bsp_oled_preblank_for_standby();
bsp_wait_keyw_low_before_standby();
pmu_wakeup_pin_enable();
pmu_to_standbymode();
```

##### Correct

```c
/* Correct: KEY3 prepares Standby, KEY4 confirms entry, KEYW/PA0 wakes later. */
bsp_oled_preblank_for_standby();
bsp_standby_preblank_indicators();
bsp_wait_key4_release_before_standby();
pmu_wakeup_pin_enable();
pmu_to_standbymode();
```

##### Wrong

```c
/* Wrong: this can leave LED3 on while waiting for Standby confirmation. */
if ((key_down_mask & BTN_KEY3_MASK) != 0U) {
    LED3_TOGGLE;
    bsp_enter_standby();
}
```

##### Correct

```c
/* Correct: Standby entry owns all indicator blanking. */
if ((key_down_mask & BTN_KEY3_MASK) != 0U) {
    bsp_enter_standby();
}
```

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

- Trigger: editing `Driver/RTC/bsp_rtc.c`, `Driver/RTC/bsp_rtc.h`, board power notes, or any startup path that calls `bsp_rtc_init()`.
- Trigger: changing RTC clock-source setup, backup-register markers, default date/time values, or `VBAT` wiring assumptions.

#### 2. Signatures

Expected resources and behavior:

```c
#define BKP_VALUE 0x32F0U

int bsp_rtc_init(void);
int bsp_rtc_get_status(bsp_rtc_status_t *status);
static uint8_t bsp_rtc_has_valid_backup(void);
static int bsp_rtc_restore_from_backup(void);
static int bsp_rtc_try_restore_lxtal_from_irc32k(uint8_t *has_valid_backup);
```

Backup-domain contract:

| Item | Required Behavior |
|------|-------------------|
| `RTC_BKP0` | Stores a project-owned marker proving the RTC backup domain was initialized before |
| Valid backup marker | Reuse current RTC time/date; do not rewrite defaults |
| Missing backup marker | Write project default time/date and then store the marker |
| `RTCSRC=IRC32K` with LXTAL now stable | Preserve a time snapshot, reset the backup domain, select LXTAL, and write the snapshot back |
| `VBAT` battery present | RTC should continue counting when main 3V3 is removed |

#### 3. Contracts

- Read the backup marker before deciding whether this boot is a restore path or a cold RTC initialization.
- If backup state is valid, do not call `rtc_init()` with default date/time values.
- If backup state is valid, do not blindly rewrite `RCU_BDCTL_RTCSRC`; preserve the running RTC clock source unless the backup domain is intentionally reset.
- If `RCU_BDCTL.RTCSRC` is `IRC32K`, first try to start LXTAL. If LXTAL stabilizes, read the current RTC time, reset the backup domain with `rcu_bkp_reset_enable()` / `rcu_bkp_reset_disable()`, select `RCU_RTCSRC_LXTAL`, restore the saved time, and rewrite `RTC_BKP0`.
- If LXTAL does not stabilize, do not reset the backup domain just to force a source change. Continue on IRC32K so the VBAT-backed time is not lost.
- Expose a read-only diagnosis path such as `bsp_rtc_get_status()` / `rtcstat` so operators can see `RTCSRC`, prescalers, backup marker state, and calibration registers.
- On restore path, call `rtc_register_sync_wait()` and then `rtc_current_time_get()` so shared runtime structures reflect the persisted RTC registers.
- Only write default date/time on first initialization or after the backup domain is known to be invalid.

#### 4. Validation & Error Matrix

| Observation | Likely Meaning | Required Action |
|-------------|----------------|-----------------|
| RTC resets to default after every wake or reboot | Startup path is rewriting RTC unconditionally | Split cold-init and backup-restore paths |
| RTC survives reset but not full power loss with battery installed | Backup marker or RTC source may be reconfigured incorrectly | Check `RTC_BKP0`, `VBAT` wiring, and `RTCSRC` rewrite behavior |
| RTC continues but is minutes fast over weeks | RTC may be running from `IRC32K`, or LXTAL load/trim is wrong | Run `rtcstat`; if `src=IRC32K`, check LXTAL startup and let firmware migrate back when stable |
| `rtcstat` shows `src=IRC32K` after a fresh boot | External 32.768kHz crystal did not stabilize, or old fallback state is still preserved | Check crystal/loads/drive first; do not mask it by silently rewriting time |
| `rtcstat` shows `recovered=1` | Firmware migrated an old IRC32K backup-domain state back to LXTAL on this boot | Run `settime` once against a trusted clock to remove accumulated RC drift |
| RTC time reads garbage after restore | Shadow registers were not resynced | Call `rtc_register_sync_wait()` before `rtc_current_time_get()` |
| No battery installed, RTC falls back to default time | Expected cold-start behavior | Document this as normal |

#### 5. Good / Base / Bad Cases

| Case | Expected Result |
|------|-----------------|
| Good | `VBAT` battery installed, main power removed then restored, RTC continues from previous time |
| Good | Old backup domain was using `IRC32K`, LXTAL is now stable, boot migrates to `LXTAL` and keeps the latest readable time |
| Base | No `VBAT` battery installed, RTC reverts to default startup time after full power loss |
| Base | LXTAL still fails to stabilize, firmware preserves the old `IRC32K` RTC instead of clearing backup state |
| Bad | `bsp_rtc_init()` always calls `rtc_init()` and rewrites time even when backup domain is valid |
| Bad | Firmware silently keeps `IRC32K` forever even after LXTAL is stable, causing large long-term drift with a coin-cell battery installed |

#### 6. Tests Required

- Power the board from main `3V3`, set/observe a non-default RTC time, then remove only main power while keeping `VBAT` present; after restore, confirm the time advanced instead of resetting.
- Repeat with no `VBAT` battery installed; confirm the board falls back to the documented default startup time.
- Confirm deep-sleep wake still keeps RTC readable after `bsp_rtc_init()` is re-entered.
- Run `rtcstat` after boot and confirm normal boards report `src=LXTAL`, `psc_a=127`, and `psc_s=255`.
- For a board that previously fell back to IRC32K, confirm the first successful LXTAL boot reports `recovered=1`, then recalibrate wall time with `settime`.

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

System bring-up is intentionally serialized in `Function/scheduler.c::system_init()`.
If a new peripheral has dependencies, place it in the correct boot order and log the stage if useful.

### Fixed-width integer types for hardware-facing data

Use `uint8_t`, `uint16_t`, `uint32_t`, and explicit buffer-size macros for data that crosses registers, DMA, or storage boundaries.

### Bounded buffer operations

Good existing examples:

- `vsnprintf(buffer, sizeof(buffer), ...)`
- UART copy length clamp in `USART0_IRQHandler()`
- file readback length checks in SMARTFS self-test and UART shell paths

### Backward-compatible wrappers when renaming public interfaces

Keep compatibility wrappers only when an old API name still exists in active call sites or current documentation.

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

- `smart_storage_self_test()` for normal GD25QXX SMARTFS validation
- `test_spi_flash()` only for destructive raw GD25QXX driver validation
- boot logs in `system_init()`

SMARTFS now owns the whole 2MB GD25Q16 device. Raw Flash erase/write tests must
be opt-in, and `SPI_FLASH_RAW_TEST_ENABLE` must stay disabled by default. If it
is enabled for driver bring-up, it erases address `0x000000` and invalidates
SMARTFS metadata by design.

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
