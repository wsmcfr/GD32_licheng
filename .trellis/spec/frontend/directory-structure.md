# Directory Structure

> How application-task and user-visible firmware code is organized.

---

## Overview

All user-facing firmware behavior lives under `USER/App/`.
Each file usually represents one app concern that is either:

- scheduled periodically by `scheduler_run()`
- initialized once during boot
- triggered indirectly by callbacks or shared state

---

## Directory Layout

```text
USER/App/
├── scheduler.c / scheduler.h   # Startup sequence and periodic task table
├── led_app.c / led_app.h       # LED state presentation
├── btn_app.c / btn_app.h       # Key event behavior and deep-sleep trigger
├── oled_app.c / oled_app.h     # OLED text composition
├── usart_app.c / usart_app.h   # UART frame echo / formatting utilities
├── adc_app.c / adc_app.h       # ADC-to-DAC or sampled data app logic
├── rtc_app.c / rtc_app.h       # RTC display formatting
└── sd_app.c / sd_app.h         # SD card demo and storage smoke tests
```

---

## Module Organization

### One app concern per file pair

Create a new `*_app.c/.h` pair when the behavior is:

- user-visible
- scheduled independently
- based on a single interaction concern

Examples:

- OLED formatting belongs in `oled_app`
- button-to-action policy belongs in `btn_app`
- storage demo behavior belongs in `sd_app`

### Central scheduling stays in `scheduler.c`

Do not scatter task-period definitions across feature files.
New periodic work should be registered in the static `scheduler_task[]` table.

### App code consumes lower layers, but does not own hardware resources

App modules may read:

- `adc_value`
- key read macros
- `rx_flag`
- RTC state

But pin definitions, DMA channels, SPI mode, and IRQ enables remain below in `Driver/` or `Component/`.

---

## Naming Conventions

- app files use the suffix `_app`
- periodic task entry functions usually use the suffix `_task`
- init functions use verb-first names such as `app_btn_init`
- app-owned global buffers or flags use descriptive names such as `uart_dma_buffer`, `rx_flag`, `ucLed`

Task naming examples:

- `led_task`
- `btn_task`
- `oled_task`
- `uart_task`
- `rtc_task`

---

## Examples

| File | Why It Is A Good Example |
|------|--------------------------|
| `USER/App/scheduler.c` | Shows centralized boot and periodic task ownership |
| `USER/App/btn_app.c` | Maps low-level key state to user-visible actions through periodic polling |
| `USER/App/oled_app.c` | Keeps display formatting separate from OLED driver primitives |
| `USER/App/usart_app.c` | Splits ISR capture from app-level string output |

### Common Placement Mistakes

- Do not put display formatting into the OLED component driver
- Do not put button business actions into `bsp_key.c`
- Do not let ISR files become application modules
