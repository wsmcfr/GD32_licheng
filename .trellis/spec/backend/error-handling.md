# Error Handling

> How low-level failures, status codes, and fatal conditions are handled in this project.

---

## Overview

This project does not use exceptions or a centralized error framework.
Error handling is done with a small set of concrete mechanisms:

- **fatal CPU faults**: enter an infinite loop
- **assertion failures**: print a debug message, then stop
- **library or driver failures**: check the return code immediately, log it, and usually return from the demo path
- **ISR handoff validation**: reject invalid lengths before copying shared buffers

---

## Error Types

| Error Surface | Where It Appears | Handling Style |
|---------------|------------------|----------------|
| CPU fault handlers | `User/gd32f4xx_it.c` | Infinite loop for fail-stop debugging |
| Assertion backend | `User/main.c` retarget/assert support | UART log + infinite loop |
| Storage status enums | `SMART_STORAGE_ERR_*`, `ErrStatus` | Check immediately and branch |
| GD25QXX write/erase helpers | `int` status from `spi_flash_*write*`, `spi_flash_*erase*`, `spi_flash_write_enable()` | Abort the higher-level storage operation and return/log IO failure |
| GD30AD3344 sampling | `GD30AD3344_AD_Read(..., &out_voltage_v)` returns `0/-1`; `GD30AD3344_GetLastError()` exposes the last DMA/parameter error | Do not use the output value when the read failed |
| Soft runtime flags | `rx_flag` in UART flow | Set/clear flag and return early |

Example fatal handler from `User/gd32f4xx_it.c`:

```c
void HardFault_Handler(void)
{
    while(1) {
    }
}
```

Example assert behavior:

```c
my_printf(DEBUG_USART, "ASSERT: %s, file: %s, line: %d\r\n", ...);
while (1) {
}
```

---

## Error Handling Patterns

### 1. Check result codes at the call site

The project does not defer status handling to a later layer.
When using SMARTFS helpers, SPI Flash helpers, or vendor APIs, check the result immediately.

Example:

```c
err = smart_storage_write_file(path, data, length);
if (SMART_STORAGE_ERR_OK != err) {
    my_printf(DEBUG_USART, "SMARTFS: write failed (%d)\r\n", err);
    return;
}
```

Example GD30AD3344 sampling behavior:

```c
if (0 != GD30AD3344_AD_Read(PT100_ADC_CHANNEL, PT100_ADC_PGA, &adc_voltage_v)) {
    s_pt100_latest.sample_ready = 0U;
    s_pt100_latest.range_valid = 0U;
    my_printf(DEBUG_USART, "PT100: sample failed err=%u\r\n",
              (unsigned int)GD30AD3344_GetLastError());
    return;
}
```

### 2. Bound-check before copying cross-layer buffers

ISR code must validate received length before copying into an app-owned buffer:

```c
if((rx_len > 0U) && (rx_len <= sizeof(usart0_rxbuffer))){
    copy_len = rx_len;
    if(copy_len >= sizeof(uart_dma_buffer)){
        copy_len = sizeof(uart_dma_buffer) - 1U;
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

`USART0_IRQHandler()` is the model pattern. It does not format output or process protocol logic in the ISR itself.

---

## Runtime Error Surfaces

| Function Family | Failure Signal | Expected Caller Behavior |
|-----------------|----------------|--------------------------|
| SMARTFS metadata init | `SMART_STORAGE_ERR_*` | Reject invalid metadata or bad pointers, do not continue with a corrupt image |
| GD25QXX write/erase | `-1` from `spi_flash_write_enable()`, `spi_flash_sector_erase()`, `spi_flash_page_write()`, or `spi_flash_buffer_write()` | Return `SMART_STORAGE_ERR_IO` or stop the destructive raw test before readback verification |
| GD30AD3344 ADC read | `-1` from `GD30AD3344_AD_Read()` | Clear app-visible sample validity and do not convert the failed raw value into voltage, resistance, or temperature |
| Timebase setup | implicit fatal loop | Treat as unrecoverable startup failure |
| UART receive handoff | `rx_flag` stays `0` | Task returns immediately without processing |

---

## Common Mistakes

### Continuing after a failed storage operation

Do not keep reading or verifying if `smart_storage_write_file()`, `smart_storage_read_file()`, or metadata loading already failed.
Return after logging.

### Copying DMA data without a length guard

Always validate the DMA-derived length before `memcpy()`.
`User/gd32f4xx_it.c` is the reference implementation.

### Logging from the wrong place

Do not add verbose logging inside hot ISR paths.
Capture data in the ISR and log from the scheduled task instead.

### Using fail-stop loops for recoverable demo failures

Storage readback mismatches and mount failures should usually log and return.
Reserve infinite loops for fatal system conditions.
