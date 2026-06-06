# Logging Guidelines

> Formal no-debug-output rules for this firmware project.

---

## Overview

This formal CIMC App target does not provide a debug logging API.

- `my_printf()` and `DEBUG_USART` have been removed from the App source tree.
- Startup, PT100, GD30AD3344, and assertion debug text must not be added back to
  the formal App path.
- `printf()` is handled through `_sys_write()`, while `_ttywrch()` remains only to satisfy the
  Arm C library and to prevent semihosting traps; they directly discard output.

Formal firmware may emit bytes on USART1/RS485 only through contest protocol
response frames.

---

## What Not To Add

- repeated per-tick messages from fast tasks such as the 10 ms OLED refresh task
- verbose formatted output inside interrupt handlers
- large raw binary buffers without length control
- hardware values on every poll when only state transitions matter
- fast sensor tasks printing formatted floating-point diagnostic lines
- boot progress strings such as `BOOT: adc init...`
- assertion text strings on USART1/RS485
- temporary `DEBUG_USART`, `CIMC_DEBUG_LOG_ENABLE`, or `my_printf()` compatibility
  wrappers

Example rule:

- `USART1_IRQHandler()` should capture data and re-arm DMA only
- `uart_task()` may parse and respond with contest protocol frames later

This keeps interrupt latency predictable and avoids polluting the contest RS485
protocol stream.

---

## ARMCLANG Retargeting And Semihosting

For the BootLoader App target, `printf()` support must remain usable without a debugger attached.

| Contract | Required Behavior |
|----------|-------------------|
| `__use_no_semihosting` | Must be present for ARMCLANG / AC6 builds so the C library does not use debugger-hosted semihosting services |
| `_sys_open()` | May only accept `stdin`, `stdout`, and `stderr`; normal file opens must fail instead of falling back to host files |
| `_sys_write()` / `_ttywrch()` | Must directly discard output; no debug backend is provided in formal App builds |
| `_sys_read()` | Must return immediately when no input backend exists; startup code must never wait for host input |
| `_sys_exit()` | Must not attempt to return to a host process; use a fail-stop loop for bare-metal firmware |

Validation:

| Check | Expected Evidence |
|-------|-------------------|
| Build log | `project/output/Project.build_log.htm` reports `0 Error(s)` |
| Link map | `project/Listings/Project.map` resolves `_sys_open`, `_sys_write`, `_sys_exit`, and `_ttywrch` to `main.o` |
| Source search | No `my_printf`, `DEBUG_USART`, or `CIMC_DEBUG_LOG_ENABLE` references in App source |
| Standalone boot | App reaches `system_init()` without semihosting traps; protocol output remains contest-framed |

If a debugger stops at `BKPT 0xAB` with a stack such as `_sys_open -> freopen -> __rt_lib_init`, treat it as semihosting leakage. Do not work around that symptom by changing BootLoader jump addresses or vendor `SystemInit()` code.
