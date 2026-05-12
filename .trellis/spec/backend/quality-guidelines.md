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
