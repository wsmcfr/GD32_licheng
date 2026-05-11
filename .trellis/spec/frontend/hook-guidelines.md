# Hook Guidelines

> Callback-driven and periodic-processing patterns in this project.

---

## Overview

This C firmware project has no React-style hooks.
The equivalent reusable logic patterns are:

- periodic task functions called by the scheduler
- callback pairs registered into libraries such as `ebtn`
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
- `rtc_task()`

If the logic needs periodic execution and no external framework owns the timing, this is the default pattern.

### Library callback pattern

When integrating an event framework, register two small callbacks:

- state reader
- event handler

Example from `USER/App/btn_app.c`:

```c
ebtn_init(btns, EBTN_ARRAY_SIZE(btns), NULL, 0, prv_btn_get_state, prv_btn_event);
```

This is the preferred pattern for debounced key handling.

### ISR-to-task handoff pattern

When data originates from an interrupt:

1. ISR validates and copies minimal data
2. ISR sets a completion flag
3. app task consumes and clears the flag later

Reference pair:

- `USER/gd32f4xx_it.c::USART0_IRQHandler()`
- `USER/App/usart_app.c::uart_task()`

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
