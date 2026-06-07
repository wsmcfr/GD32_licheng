# Directory Structure

> How application-task and user-visible firmware code is organized.

---

## Overview

All user-facing firmware behavior lives under `Function/`.
Each file usually represents one app concern that is either:

- scheduled periodically by `scheduler_run()`
- initialized once during boot
- triggered indirectly by callbacks or shared state

---

## Directory Layout

```text
Function/
├── scheduler.c / scheduler.h   # Startup sequence and periodic task table
├── led_app.c / led_app.h       # Two formal LEDs: system and auto-sample status
├── oled_app.c / oled_app.h     # Two-line OLED text composition
├── usart_app.c / usart_app.h   # USART1/RS485 ISR-to-task frame handoff
├── cimc_status.c / cimc_status.h # Team ID and auto-sample status
├── adc_app.c / adc_app.h       # ADC/DAC app logic; DAC is controlled by 0x0301
├── gd30ad3344_pt100_app.c / gd30ad3344_pt100_app.h # GD30AD3344 PT100 resistance/temperature conversion
└── rtc_app.c / rtc_app.h       # Unix timestamp conversion helpers used by CIMC protocol
```

Legacy `btn_app.c`, `uart_ota_app.c`, and USART0/SMARTFS shell behavior have been removed from the formal source tree and must not be restored as CIMC build entries.

---

## Module Organization

### One app concern per file pair

Create a new `*_app.c/.h` pair when the behavior is:

- user-visible
- scheduled independently
- based on a single interaction concern

Examples:

- OLED formatting belongs in `oled_app`
- contest UART frame handoff belongs in `usart_app`
- contest frame parsing belongs in `Protocol/cimc_protocol.c`
- PT100 conversion policy belongs in `gd30ad3344_pt100_app`

### Central scheduling stays in `scheduler.c`

Do not scatter task-period definitions across feature files.
New periodic work should be registered in the static `scheduler_task[]` table.

### App code consumes lower layers, but does not own hardware resources

App modules may read:

- `adc_value`
- `g_idle_pend`
- RTC state through `rtc_app_get_unix_epoch()` / `rtc_app_set_unix_epoch()`
- status from `cimc_status`

But pin definitions, DMA channels, SPI mode, and IRQ enables remain below in `Driver/` or `Library/`.

---

## Naming Conventions

- app files use the suffix `_app`
- periodic task entry functions usually use the suffix `_task`
- init/setter functions use verb-first names such as `cimc_status_set_auto_sample`
- app-owned shared flags use descriptive names such as `g_idle_pend`

Task naming examples:

- `led_task`
- `oled_task`
- `uart_task`
- `adc_task`

---

## Examples

| File | Why It Is A Good Example |
|------|--------------------------|
| `Function/scheduler.c` | Shows centralized boot and periodic task ownership |
| `Function/oled_app.c` | Keeps display formatting separate from OLED driver primitives and limits formal display to two rows |
| `Function/usart_app.c` | Splits USART1 ISR capture from task-level contest protocol processing |
| `Function/adc_app.c` | Keeps DAC command control separate from ADC sampling so `0x0301` is not overwritten by CH0 |

### Common Placement Mistakes

- Do not put display formatting into the OLED component driver
- Do not put contest frame parsing or CRC work into `User/gd32f4xx_it.c`
- Do not let ISR files become application modules
