# Hook Guidelines

> Callback-driven and periodic-processing patterns in this project.

---

## Overview

This C firmware project has no React-style hooks.
The equivalent reusable logic patterns are:

- periodic task functions called by the scheduler
- small private helper pairs inside polling modules
- ISR-to-task handoff through shared flags and buffers

Use this file when adding repeated event/time-driven glue logic.

---

## Custom Hook Patterns

### Scheduler-driven periodic hook pattern

Most app logic is expressed as a `*_task(void)` function added to `scheduler_task[]`.

Good examples:

- `led_task()`
- `adc_task()`
- `oled_task()`
- `btn_task()`
- `uart_task()`
- `uart_ota_task()`
- `rtc_task()`

If the logic needs periodic execution and no external framework owns the timing, this is the default pattern.

### Polling helper pattern

When a feature only needs stable edge detection and simple action mapping, keep two small private helpers:

- a raw state reader
- a dispatch helper for the derived event

Example from `USER/App/btn_app.c`:

```c
static uint8_t prv_btn_read_mask(void);
static void prv_btn_dispatch(uint8_t key_down_mask);
```

This is the preferred pattern for lightweight debounced key handling when no richer button framework is needed.

### ISR-to-task handoff pattern

When data originates from an interrupt:

1. ISR validates and copies minimal data
2. ISR sets a completion flag
3. app task consumes and clears the flag later

Reference pair:

- `USER/gd32f4xx_it.c::USART0_IRQHandler()`
- `USER/App/usart_app.c::uart_task()`
- `USER/gd32f4xx_it.c::USART1_IRQHandler()`
- `USER/App/uart_ota_app.c::uart_ota_task()`

---

## Data Fetching

The firmware equivalent of “data fetching” is sampling or reading from peripherals.
Rules:

- low-level acquisition remains in driver/ISR/component code
- app tasks only consume already prepared buffers or public state
- app tasks should not duplicate raw acquisition setup

Examples:

- `adc_task()` consumes `adc_value[]`
- `rtc_task()` reads current RTC data and formats it for display
- `uart_task()` consumes `uart_dma_buffer` after ISR handoff
- `uart_ota_task()` consumes `uart_ota_dma_buffer` after RS485/USART1 ISR handoff and sends OTA ACK later

---

## Naming Conventions

- periodic entries: `<feature>_task`
- app init entries: `app_<feature>_init`
- private callbacks/helpers: `prv_<name>`
- shared event flags: short descriptive nouns such as `rx_flag`

---

## Common Mistakes

### Doing the whole job in the ISR

Keep ISR work minimal and defer app behavior.

### Adding periodic behavior without registering it centrally

If a task is meant to run periodically, it must be visible in `scheduler_task[]`.

### Duplicating sampling logic in multiple app files

Share the lower-layer source of truth instead of reconfiguring the same peripheral from several app modules.
