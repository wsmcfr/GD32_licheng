# Quality Guidelines

> Code quality standards for app-task and user-visible firmware behavior.

---

## Overview

Frontend quality in this project means:

- predictable periodic behavior
- clear separation between display/interaction logic and low-level drivers
- bounded formatting and buffer usage
- observable behavior on OLED, two formal LEDs, USART1/RS485, ADC/DAC, RTC, and PT100 sampling

---

## Forbidden Patterns

### Do not block for long inside fast periodic tasks

Tasks like `oled_task()` and `uart_task()` should stay lightweight.
Heavy I/O, large retries, or long loops belong in startup tests or explicit demo flows, not in high-frequency display/input tasks.

### Do not make app tasks own hardware reconfiguration

App tasks may request behavior such as Bootloader upgrade entry, but the actual Flash handoff belongs in driver-level code like `bootloader_port.c`.

### Do not bypass the scheduler for periodic behavior

If logic is intended to run every few milliseconds, register it in `scheduler_task[]` instead of hiding it in an unrelated busy loop.

### Do not use unbounded formatting for user-visible strings

Prefer `vsnprintf()` as used by:

- `oled_printf()`

---

## Required Patterns

### Keep app modules small and focused

One file pair should usually own one concern:

- display
- LED behavior
- UART frame handoff
- RTC cache refresh
- ADC/DAC command state

### Use task functions as the stable integration seam

The scheduler task table is the project’s main composition point for user-visible periodic logic.

### Defer cross-layer processing out of ISR

If data comes from an interrupt, the app task should process it later using a flag/buffer handoff.

### Favor readable user-facing output

OLED strings should stay within two formal lines. USART1 output must stay contest-framed, not human-readable debug text.

---

## Testing Requirements

Validate app-facing changes through the actual visible behavior:

| Area | Minimum Validation |
|------|--------------------|
| OLED text changes | only two lines are used and content fits screen |
| LED behavior | LED1 blinks at the formal system period; LED2 follows auto-sample state |
| UART app behavior | USART1 frame is copied, parsed, answered, and cleared correctly |
| Scheduler changes | new task runs at the expected period and does not starve other tasks |
| RTC / ADC / DAC / PT100 | cached values update and DAC is not overwritten by ADC polling |

Where possible, verify using:

- OLED screen output
- LED state changes
- USART1/RS485 protocol frames
- Keil build logs

---

## Code Review Checklist

- Is the app code in `Function/` instead of leaking into `Driver/`, `Protocol/`, or library layers?
- Does the module expose only the public functions or shared buffers it needs?
- Is periodic behavior registered centrally in `scheduler_task[]`?
- Are display and logging buffers bounded?
- Does the task remain lightweight for its configured period?
- If data comes from ISR or DMA, is there a clear handoff boundary?
- Is the visible behavior testable on real hardware?
