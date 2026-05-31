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
- `uart_ota_rx_flag`
- `uart_ota_dma_buffer`
- `ucLed[6]` through `led_app_set()`, `led_app_toggle()`, `led_app_all_off()`, and `led_app_blank_for_sleep()`

### Module-Private Cached State

Used to suppress redundant hardware updates or hide internal implementation details.

Examples:

- `static uint8_t g_led_mask_old` and `static uint8_t g_led_cache_valid` inside `led_app.c` force the first LED write, then cache the previous bitmap so only changed LEDs are written
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
- `uart_ota_rx_flag` is global because it is written in `USART1_IRQHandler()` and consumed in `uart_ota_task()`

Bad example:

- a local formatting scratch buffer does not need to be global

---

## Derived State

Derived user-facing state should usually be recomputed inside the task that renders or consumes it.

Examples:

- `oled_task()` derives voltage text from `adc_value[]`
- `led_disp()` derives a bitmask from `ucLed[]`; button and power policy code must change LEDs through the public `led_app_*` APIs so the app state and hardware cache stay aligned
- `rtc_task()` derives display text from `rtc_initpara`
- `gd30ad3344_pt100_task()` derives voltage, resistance, temperature, `sample_ready`, and `range_valid` from a successful `GD30AD3344_AD_Read(..., &out_voltage_v)` call

This keeps the source of truth close to the render/output path.

### Error-Derived Validity Flags

App-facing state that depends on a hardware sample must carry validity separately from the last numeric value.
For GD30AD3344/PT100:

```c
if (0 != GD30AD3344_AD_Read(PT100_ADC_CHANNEL, PT100_ADC_PGA, &adc_voltage_v)) {
    s_pt100_latest.sample_ready = 0U;
    s_pt100_latest.range_valid = 0U;
    return;
}
```

Contracts:

- `sample_ready == 1` means the cached PT100 numbers came from a successful current sample.
- `range_valid == 0` can mean either the hardware sample failed or the converted temperature was clamped outside the supported range; logs should distinguish these cases.
- Do not overwrite voltage/resistance/temperature with data converted from a failed SPI/DMA read. Keeping the previous numeric values plus clearing validity is safer for display and diagnostics.

---

## Common Mistakes

### Duplicating the same state in multiple modules

Prefer one shared source of truth plus derived formatting in the consumer task.

### Forgetting to clear handoff flags

After consuming an ISR-produced frame, clear the app flag and reset temporary buffers if needed.
`uart_task()` is the reference pattern.

### Updating hardware every cycle without change detection

`led_disp()` keeps `g_led_mask_old` plus a first-run valid flag and compares the cached bitmap with the current bitmap to avoid redundant writes.
Follow that idea when output hardware changes are expensive or noisy.

When another layer resets GPIO output state, turns LEDs off for sleep, or reruns `bsp_led_init()`, call `led_app_reset_cache()` before normal scheduling resumes. Otherwise the app task may believe the old bitmap is still present on the pins and skip a required refresh.

### Treating failed sensor reads as valid data

Do not convert GD30AD3344 DMA timeout sentinels such as `0xFFFF` into voltage or temperature.
Check `GD30AD3344_AD_Read()` first, clear the app-visible validity flags on failure, and return before publishing derived state.
