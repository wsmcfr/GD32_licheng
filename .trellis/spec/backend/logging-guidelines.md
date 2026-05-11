# Logging Guidelines

> Debug UART logging conventions for this firmware project.

---

## Overview

This project does not use a dedicated logging framework.
Logging is done through:

- `my_printf(DEBUG_USART, ...)` in app and test code
- `printf()` / `fputc()` redirection in `USER/main.c`

The output target is the debug UART, currently `USART0`.

---

## Log Levels

There is no enforced enum-based log level system.
Instead, the project uses message prefixes and context.

| Style | When To Use | Example |
|-------|-------------|---------|
| `BOOT:` | startup sequencing and init milestones | `BOOT: adc init...` |
| Module prefix | module-specific diagnostics or demos | `FATFS long-name PASS:` |
| `ASSERT:` | fatal assertion reporting | `ASSERT: expr, file, line` |
| Plain echo | simple demo behavior such as UART pass-through | `my_printf(DEBUG_USART, "%s", uart_dma_buffer);` |

If you add a new repeated log stream, prefer a short stable prefix instead of free-form text.

---

## Structured Logging

The project uses **line-oriented human-readable logs**, not JSON.
Good log lines usually contain:

- a short module or phase prefix
- the operation name
- the result code or key numeric value
- `\r\n` line endings for serial terminals

Examples from the existing codebase:

```c
my_printf(DEBUG_USART, "BOOT: rtc init...\r\n");
my_printf(DEBUG_USART, "SD Card f_mount:%d\r\n", result);
my_printf(DEBUG_USART, "ASSERT: %s, file: %s, line: %d\r\n", ...);
```

Prefer one-line messages that make serial logs scannable.

---

## What to Log

- startup stage boundaries in `system_init()`
- hardware identification values such as flash ID or SD card type
- result codes for mount/open/read/write operations
- PASS / FAIL outcomes for self-tests
- assertion context for fatal failures

Good examples:

- `USER/App/sd_app.c::card_info_get()` prints card type, size, and IDs
- `USER/App/sd_app.c::sd_fatfs_long_name_test()` prints exact failure points
- `USER/App/scheduler.c::system_init()` prints boot progress around major peripherals

---

## What NOT to Log

- repeated per-tick messages from fast tasks such as the 10 ms OLED refresh task
- verbose formatted output inside interrupt handlers
- large raw binary buffers without length control
- hardware values on every poll when only state transitions matter

Example rule:

- `USART0_IRQHandler()` should capture data and re-arm DMA
- `uart_task()` may log or echo the completed frame later

This keeps interrupt latency predictable and avoids flooding the serial console.
