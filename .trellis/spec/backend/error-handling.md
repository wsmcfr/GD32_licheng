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
| CPU fault handlers | `USER/gd32f4xx_it.c` | Infinite loop for fail-stop debugging |
| Assertion backend | `USER/App/sd_app.c::__aeabi_assert` | UART log + infinite loop |
| Vendor status enums | `FRESULT`, `DSTATUS`, `sd_error_enum`, `ErrStatus` | Check immediately and branch |
| Soft runtime flags | `rx_flag` in UART flow | Set/clear flag and return early |

Example fatal handler from `USER/gd32f4xx_it.c`:

```c
void HardFault_Handler(void)
{
    while(1) {
    }
}
```

Example assert backend from `USER/App/sd_app.c`:

```c
my_printf(DEBUG_USART, "ASSERT: %s, file: %s, line: %d\r\n", ...);
while (1) {
}
```

---

## Error Handling Patterns

### 1. Check result codes at the call site

The project does not defer status handling to a later layer.
When using FatFs, SDIO helpers, or vendor APIs, check the result immediately.

Example:

```c
fres = f_open(&long_file, long_path, FA_CREATE_ALWAYS | FA_WRITE);
if (FR_OK != fres) {
    my_printf(DEBUG_USART, "FATFS long-name open(write) failed (%d)\r\n", fres);
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
| FatFs operations | `FRESULT` | Log the code, stop the current demo step, close handles if needed |
| SD physical init | `DSTATUS` / `sd_error_enum` | Retry if appropriate, then log the final status |
| LittleFS config init | `LFS_ERR_*` | Reject invalid config, do not continue with a bad pointer |
| Timebase setup | implicit fatal loop | Treat as unrecoverable startup failure |
| UART receive handoff | `rx_flag` stays `0` | Task returns immediately without processing |

---

## Common Mistakes

### Continuing after a failed storage operation

Do not keep reading or verifying if `f_open()`, `f_write()`, or `f_read()` already failed.
Return after logging.

### Copying DMA data without a length guard

Always validate the DMA-derived length before `memcpy()`.
`USER/gd32f4xx_it.c` is the reference implementation.

### Logging from the wrong place

Do not add verbose logging inside hot ISR paths.
Capture data in the ISR and log from the scheduled task instead.

### Using fail-stop loops for recoverable demo failures

Storage readback mismatches and mount failures should usually log and return.
Reserve infinite loops for fatal system conditions.
