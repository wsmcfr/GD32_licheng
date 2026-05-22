# Directory Structure

> How low-level firmware code is organized in this project.

---

## Overview

Backend code is organized by responsibility instead of by abstract service layers.
The main rule is: **hardware ownership stays low, app behavior stays high**.

---

## Directory Layout

```text
USER/
├── main.c / main.h                # Program entry and C library retarget
├── systick.c / systick.h          # Millisecond tick and blocking delay
├── gd32f4xx_it.c / gd32f4xx_it.h  # Interrupt service routines
├── system_all.h                   # Shared aggregation header
├── Driver/
│   ├── bsp_led.c/h
│   ├── bsp_key.c/h
│   ├── bsp_oled.c/h
│   ├── bsp_storage.c/h
│   ├── bsp_usart.c/h
│   ├── bsp_analog.c/h
│   ├── bsp_rtc.c/h
│   └── bsp_power.c/h
├── Component/
│   ├── oled/
│   ├── gd25qxx/
│   ├── gd30ad3344/
│   ├── sdio/
│   ├── fatfs/
│   └── ebtn/
└── App/
    ├── scheduler.c/h
    ├── led_app.c/h
    ├── btn_app.c/h
    ├── oled_app.c/h
    ├── usart_app.c/h
    ├── adc_app.c/h
    ├── rtc_app.c/h
    └── sd_app.c/h
```

---

## Module Organization

### Driver Layer

Use `USER/Driver/` for board-specific resource ownership:

- pin definitions
- DMA channel mapping
- IRQ enable/disable
- peripheral clock enable
- one-shot initialization and low-power reconfiguration

Example:

- `USER/Driver/bsp_usart.c` owns USART pin mapping, DMA setup, and IDLE interrupt enable
- `USER/Driver/bsp_power.c` owns deep-sleep resource shutdown and wakeup re-init order

### Component Layer

Use `USER/Component/` for reusable device or protocol logic:

- SSD1306 display primitives in `USER/Component/oled/`
- GD25Qxx SPI Flash operations in `USER/Component/gd25qxx/`
- GD30AD3344 command/data protocol in `USER/Component/gd30ad3344/`
- third-party libraries or storage ports such as FatFs, SMARTFS, retained LittleFS source copies, and `ebtn`

Component code may depend on driver-provided buses, but it should not become the place that owns board-level pin maps.

### Infrastructure Files

Keep global runtime infrastructure outside feature folders:

- `USER/main.c` contains the entry loop and `printf` retarget
- `USER/systick.c` contains the time base and blocking delay implementation
- `USER/gd32f4xx_it.c` contains ISR entry points
- `USER/system_all.h` centralizes shared includes and layer ordering

---

## Naming Conventions

- Board-support files use `bsp_<feature>.c/.h`
- App-facing feature files use `<feature>_app.c/.h`
- Device component folders usually use the device or protocol name directly, such as `gd25qxx/` and `oled/`
- Public APIs use descriptive verbs such as `bsp_usart_init`, `bsp_enter_deepsleep`, `spi_flash_buffer_write`, `oled_task`
- Private helpers stay `static` inside `.c` files

Header conventions:

- Public macros, `extern` buffers, and public function declarations belong in the matching `.h`
- Driver headers that include `system_all.h` while avoiding app-layer cycles use the `SYSTEM_ALL_BASE_ONLY` guard pattern

Example from `USER/Driver/bsp_storage.h`:

```c
#define SYSTEM_ALL_BASE_ONLY
#include "system_all.h"
#undef SYSTEM_ALL_BASE_ONLY
```

---

## Examples

| File | Why It Is A Good Example |
|------|--------------------------|
| `USER/system_all.h` | Shows the real include order: standard headers, common components, drivers, components, then apps |
| `USER/Driver/bsp_usart.h` | Keeps all USART pin/DMA macros and shared buffers in the public header |
| `USER/Driver/bsp_power.c` | Groups power-state transitions and wakeup recovery in one ownership unit |
| `USER/App/scheduler.c` | Keeps app task registration centralized instead of scattering scheduling logic |

### Common Placement Mistakes

- Do not place board pin macros in `App/` headers
- Do not put scheduler task registration inside unrelated feature files
- Do not expose private helper callbacks from `.c` files unless another module truly needs them
