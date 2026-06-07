# Error Handling

> How low-level failures, status codes, and fatal conditions are handled in this project.

---

## Overview

This project does not use exceptions or a centralized error framework.
Error handling is done with a small set of concrete mechanisms:

- **fatal CPU faults**: enter an infinite loop
- **assertion failures**: stop in a fail-stop loop
- **library or driver failures**: check the return code immediately and return from the current path with invalid-state flags or a contest error frame
- **ISR handoff validation**: reject invalid lengths before copying shared buffers

---

## Error Types

| Error Surface | Where It Appears | Handling Style |
|---------------|------------------|----------------|
| CPU fault handlers | `User/gd32f4xx_it.c` | Infinite loop for fail-stop debugging |
| Assertion backend | `User/main.c` retarget/assert support | No UART output; fail-stop loop only |
| Bootloader handoff status | `bootloader_port_status_t` | Send contest error frame or stay in App |
| Internal Flash / FMC status | `fmc_state_enum` or wrapper status | Abort destructive operation and preserve current App when possible |
| GD30AD3344 sampling | `GD30AD3344_AD_Read(..., &out_voltage_v)` returns `0/-1` | Do not use the output value when the read failed |
| Soft runtime flags | `g_idle_pend` in UART flow | Set/clear flag and return early |

Example fatal handler from `User/gd32f4xx_it.c`:

```c
void HardFault_Handler(void)
{
    while(1) {
    }
}
```

## Error Handling Patterns

### 1. Check result codes at the call site

The project does not defer status handling to a later layer.
When using Bootloader handoff helpers, internal Flash helpers, sensor drivers, or vendor APIs, check the result immediately.

Example:

```c
status = bootloader_port_request_bootloader_upgrade();
if (BOOTLOADER_PORT_STATUS_OK != status) {
    (void)cimc_protocol_send_error(device_id, command);
    return;
}
```

Example GD30AD3344 sampling behavior:

```c
if (0 != GD30AD3344_AD_Read(PT100_ADC_CHANNEL, PT100_ADC_PGA, &adc_voltage_v)) {
    s_pt100_latest.sample_ready = 0U;
    s_pt100_latest.range_valid = 0U;
    return;
}
```

### 2. Bound-check before copying cross-layer buffers

Task code must validate the DMA-derived length before copying into a local protocol buffer:

```c
if((rx_len > 0U) && (rx_len <= sizeof(usart1_rxbuffer))){
    copy_len = rx_len;
    if(copy_len >= sizeof(fbuf)){
        copy_len = sizeof(fbuf) - 1U;
    }
```

### 3. Use fail-stop loops only for unrecoverable states

An infinite loop is acceptable in this codebase for:

- CPU exception handlers
- `SysTick_Config()` failure in `systick.c`
- assertion backends

Do not use `while(1)` as a lazy replacement for normal error propagation in app logic.

### 4. Keep ISR recovery minimal

Interrupt handlers should:

- clear the flag
- capture the minimum required data
- restore DMA or peripheral state
- exit

`USART1_IRQHandler()` is the model pattern. It does not format output or process protocol logic in the ISR itself.

---

## Runtime Error Surfaces

| Function Family | Failure Signal | Expected Caller Behavior |
|-----------------|----------------|--------------------------|
| Bootloader wait request | `BOOTLOADER_PORT_STATUS_*` | Send error frame and do not reset when parameter write fails |
| Internal Flash erase/write | wrapper return code or FMC status | Abort upgrade/copy flow and preserve current App when possible |
| GD30AD3344 ADC read | `-1` from `GD30AD3344_AD_Read()` | Clear app-visible sample validity and do not convert the failed raw value into voltage, resistance, or temperature |
| Timebase setup | implicit fatal loop | Treat as unrecoverable startup failure |
| UART receive handoff | `g_idle_pend` stays `0` | Task returns immediately without processing |

---

## Common Mistakes

### Resetting after a failed Bootloader handoff

Do not call `NVIC_SystemReset()` after `bootloader_port_request_bootloader_upgrade()`
fails. Send a contest error frame and remain in App.

### Copying DMA data without a length guard

Always validate the DMA-derived length before `memcpy()`.
`User/gd32f4xx_it.c` is the reference implementation.

### Reintroducing debug output

Do not add `my_printf`, `DEBUG_USART`, boot-progress strings, or PT100 diagnostic
text back to the formal App path. Errors should be reflected through return
codes, validity flags, or contest protocol error frames.

### Using fail-stop loops for recoverable demo failures

Storage readback mismatches and mount failures should usually log and return.
Reserve infinite loops for fatal system conditions.
