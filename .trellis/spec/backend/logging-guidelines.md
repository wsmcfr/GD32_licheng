# Logging Guidelines

> Debug UART logging conventions for this firmware project.

---

## Overview

This project does not use a dedicated logging framework.
Logging is done through:

- `my_printf(DEBUG_USART, ...)` in app and test code
- `printf()` / `fputc()` redirection in `User/main.c`

Formal firmware must not emit debug logs on the contest RS485 bus by default.
`my_printf()` and C-library retarget output are kept for compatibility, but the
current App drops output unless `CIMC_DEBUG_LOG_ENABLE` is explicitly enabled for
local debugging.

---

## Log Levels

There is no enforced enum-based log level system.
Instead, the project uses message prefixes and context.

| Style | When To Use | Example |
|-------|-------------|---------|
| `BOOT:` | startup sequencing and init milestones | `BOOT: adc init...` |
| Module prefix | module-specific diagnostics or demos | `PT100: sample failed err=...` |
| `ASSERT:` | fatal assertion reporting | `ASSERT: expr, file, line` |
| Plain echo | simple shell behavior such as `cat` file-content echo or `ls` output | `bsp_usart_send_buffer(DEBUG_USART, uart_file_buffer, len);` |

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
my_printf(DEBUG_USART, "ASSERT: %s, file: %s, line: %d\r\n", ...);
```

Prefer one-line messages that make serial logs scannable.

---

## What to Log

- startup stage boundaries in `system_init()` when temporary debug logging is enabled
- hardware identification values needed during bring-up
- result codes for sensor, protocol, Flash, or Bootloader-handoff failures
- assertion context for fatal failures

Good examples:

- `Function/scheduler.c::system_init()` prints boot progress around major peripherals

---

## What NOT to Log

- repeated per-tick messages from fast tasks such as the 10 ms OLED refresh task
- verbose formatted output inside interrupt handlers
- large raw binary buffers without length control
- hardware values on every poll when only state transitions matter
- fast sensor tasks printing long formatted floating-point lines every sample; throttle repeated diagnostics such as PT100 measurements to a human-readable interval

Example rule:

- `USART1_IRQHandler()` should capture data and re-arm DMA only
- `uart_task()` may parse and respond with contest protocol frames later

This keeps interrupt latency predictable and avoids polluting the contest RS485 protocol stream.

---

## ARMCLANG Retargeting And Semihosting

For the BootLoader App target, `printf()` support must remain usable without a debugger attached.

| Contract | Required Behavior |
|----------|-------------------|
| `__use_no_semihosting` | Must be present for ARMCLANG / AC6 builds so the C library does not use debugger-hosted semihosting services |
| `_sys_open()` | May only accept `stdin`, `stdout`, and `stderr`; normal file opens must fail instead of falling back to host files |
| `_sys_write()` / `fputc()` / `_ttywrch()` | Must drop output by default; if a temporary debug backend is enabled, it must not pollute formal RS485 frames |
| `_sys_read()` | Must return immediately when no input backend exists; startup code must never wait for host input |
| `_sys_exit()` | Must not attempt to return to a host process; use a fail-stop loop for bare-metal firmware |

Validation:

| Check | Expected Evidence |
|-------|-------------------|
| Build log | `project/output/Project.build_log.htm` reports `0 Error(s)` |
| Link map | `project/Listings/Project.map` resolves `_sys_open`, `_sys_write`, `_sys_exit`, and `_ttywrch` to `main.o` |
| Standalone boot | App reaches `system_init()` without semihosting traps; protocol output remains contest-framed |

If a debugger stops at `BKPT 0xAB` with a stack such as `_sys_open -> freopen -> __rt_lib_init`, treat it as semihosting leakage. Do not work around that symptom by changing BootLoader jump addresses or vendor `SystemInit()` code.
