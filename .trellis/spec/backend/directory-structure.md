# Directory Structure

> How low-level firmware code is organized in this project.

---

## Overview

Backend code is organized by responsibility instead of by abstract service layers.
The main rule is: **hardware ownership stays low, app behavior stays high**.

---

## Directory Layout

```text
User/
├── main.c / main.h                # Program entry and C library retarget
├── systick.c / systick.h          # SysTick/DWT timebase and blocking delay
├── gd32f4xx_it.c / gd32f4xx_it.h  # Interrupt service routines
├── gd32f4xx_libopt.h              # GD32 standard peripheral library option header
└── boot_app_config.c / .h         # BootLoader App address and handoff recovery

Function/
├── scheduler.c / scheduler.h      # Current cooperative scheduler and startup sequence
├── led_app.c / led_app.h
├── btn_app.c / btn_app.h
├── oled_app.c / oled_app.h
├── usart_app.c / usart_app.h
├── uart_ota_app.c / uart_ota_app.h
├── adc_app.c / adc_app.h
├── gd30ad3344_pt100_app.c / gd30ad3344_pt100_app.h
└── rtc_app.c / rtc_app.h

Driver/
├── LED/
├── KEY/
├── USART/
├── OLED/
├── STORAGE/
├── ANALOG/
├── RTC/
├── POWER/
├── BOOTLOADER/
├── GD25QXX/
└── GD30AD3344/

Protocol/
└── ota_image_protocol.c / .h       # OTA header-bin protocol parsing, CRC, and metadata validation

HeaderFiles/
└── system_all.h                   # Shared aggregation header

Library/
└── GD32F4xx_standard_peripheral/  # Vendor standard peripheral library

CMSIS/                             # ARM CMSIS core and GD32 device files
Startup/                           # Keil startup assembly
project/                           # Keil project, RTE, Objects, output, Listings
```

---

## Module Organization

### Driver Layer

Use `Driver/` for board-specific resource ownership:

- pin definitions
- DMA channel mapping
- IRQ enable/disable
- peripheral clock enable
- one-shot initialization and low-power reconfiguration

Example:

- `Driver/USART/bsp_usart.c` owns USART pin mapping, DMA setup, and IDLE interrupt enable
- `Driver/POWER/bsp_power.c` owns deep-sleep resource shutdown and wakeup re-init order

### Component Layer

Use `Driver/<device>/` for reusable device logic:

- SSD1306 display primitives in `Driver/OLED/`
- GD25Qxx SPI Flash and SMARTFS operations in `Driver/GD25QXX/`
- GD30AD3344 command/data protocol in `Driver/GD30AD3344/`

### Protocol Layer

Use `Protocol/` for packet/file-format contracts that are consumed by app tasks but do not own hardware:

- OTA header parsing and CRC helpers in `Protocol/ota_image_protocol.c`
- metadata validation for magic, header size, load address, payload size, and vector-table fields
- no DMA, UART, Flash erase/write, scheduler, or BootLoader parameter writes in this layer

Component code may depend on driver-provided buses, but it should not become the place that owns board-level pin maps.

Keil project display should keep all low-level source entries under a single
`Driver` group. Do not create separate uVision groups such as
`Driver/OLED` or `Driver/GD25QXX`; the physical
subdirectories still carry ownership boundaries, while the IDE tree stays
compact and consistent with the top-level firmware layer.

### Function Layer

Use `Function/` for scheduled application behavior and user-visible policy:

- keep the existing `Function/scheduler.c` cooperative scheduler as the boot and task dispatch framework
- place new `*_app.c/.h` task modules here
- do not switch this project to the template `Function.c` / `UsrFunction()` loop style unless the scheduler framework is intentionally replaced

### Infrastructure Files

Keep global runtime infrastructure outside feature folders:

- `User/main.c` contains the entry loop and `printf` retarget
- `User/systick.c` contains the project-owned SysTick/DWT timebase, blocking delay implementation, and clock-change/deep-sleep reconfiguration hooks
- `User/gd32f4xx_it.c` contains ISR entry points
- `HeaderFiles/system_all.h` centralizes shared includes and layer ordering

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

Example from `Driver/STORAGE/bsp_storage.h`:

```c
#define SYSTEM_ALL_BASE_ONLY
#include "system_all.h"
#undef SYSTEM_ALL_BASE_ONLY
```

---

## Examples

| File | Why It Is A Good Example |
|------|--------------------------|
| `HeaderFiles/system_all.h` | Shows the real include order: standard headers, common components, drivers, components, then apps |
| `Driver/USART/bsp_usart.h` | Keeps all USART pin/DMA macros and shared buffers in the public header |
| `Driver/POWER/bsp_power.c` | Groups power-state transitions and wakeup recovery in one ownership unit |
| `Protocol/ota_image_protocol.c` | Keeps OTA file-format parsing and CRC checks out of the app task state machine |
| `Function/scheduler.c` | Keeps app task registration centralized instead of scattering scheduling logic |

### Common Placement Mistakes

- Do not place board pin macros in `App/` headers
- Do not put scheduler task registration inside unrelated feature files
- Do not expose private helper callbacks from `.c` files unless another module truly needs them
