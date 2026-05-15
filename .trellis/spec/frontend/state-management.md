# State Management

> How app-visible state is owned and exchanged in this firmware project.

---

## Overview

State management is simple and mostly static:

- global arrays or flags for cross-module runtime state
- static local state for module-private caches
- driver-owned shared buffers exposed through headers
- task polling plus callback processing instead of message queues

---

## State Categories

### Driver-Owned Shared State

Used when hardware-facing buffers must be visible across modules.

Examples:

- `adc_value[2]` in `bsp_analog.h`
- `usart0_rxbuffer[]` and friends in `bsp_usart.h`
- `oled_cmd_buf[2]` and `oled_data_buf[2]` in `bsp_oled.h`

### App-Owned Shared State

Used when both ISR and app logic need a shared runtime bridge, or when several app functions share user-facing state.

Examples:

- `rx_flag`
- `uart_dma_buffer`
- `rs485_rx_flag`
- `rs485_dma_buffer`
- `ucLed[6]`

### Module-Private Cached State

Used to suppress redundant hardware updates or hide internal implementation details.

Examples:

- `static uint8_t temp_old = 0xff;` inside `led_disp()`
- `static task_t scheduler_task[]` and `static uint8_t task_num` in `scheduler.c`

---

## When to Use Global State

Use global or header-declared shared state only when one of these is true:

- a driver and an app task both need the same buffer
- an ISR produces data that an app task consumes
- the state represents a stable hardware or app contract

Do **not** create global state just to avoid passing one parameter through a private helper.

Good example:

- `rx_flag` is global because it is written in `USART0_IRQHandler()` and consumed in `uart_task()`
- `rs485_rx_flag` is global because it is written in `USART1_IRQHandler()` and consumed in `rs485_task()`

Bad example:

- a local formatting scratch buffer does not need to be global

---

## Derived State

Derived user-facing state should usually be recomputed inside the task that renders or consumes it.

Examples:

- `oled_task()` derives voltage text from `adc_value[]`
- `led_disp()` derives a bitmask from `ucLed[]`
- `rtc_task()` derives display text from `rtc_initpara`

This keeps the source of truth close to the render/output path.

---

## Common Mistakes

### Duplicating the same state in multiple modules

Prefer one shared source of truth plus derived formatting in the consumer task.

### Forgetting to clear handoff flags

After consuming an ISR-produced frame, clear the app flag and reset temporary buffers if needed.
`uart_task()` is the reference pattern.

### Updating hardware every cycle without change detection

`led_disp()` keeps `temp_old` to avoid redundant writes.
Follow that idea when output hardware changes are expensive or noisy.
