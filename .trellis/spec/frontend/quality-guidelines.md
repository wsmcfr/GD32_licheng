# Quality Guidelines

> Code quality standards for app-task and user-visible firmware behavior.

---

## Overview

Frontend quality in this project means:

- predictable periodic behavior
- clear separation between display/interaction logic and low-level drivers
- bounded formatting and buffer usage
- observable behavior on OLED, LEDs, buttons, UART, and storage demos

---

## Forbidden Patterns

### Do not block for long inside fast periodic tasks

Tasks like `oled_task()` and `btn_task()` should stay lightweight.
Heavy I/O, large retries, or long loops belong in startup tests or explicit demo flows, not in high-frequency display/input tasks.

### Do not make app tasks own hardware reconfiguration

App tasks may trigger behavior such as deep sleep, but the actual peripheral shutdown/re-init logic belongs in driver-level code like `bsp_power.c`.

### Do not bypass the scheduler for periodic behavior

If logic is intended to run every few milliseconds, register it in `scheduler_task[]` instead of hiding it in an unrelated busy loop.

### Do not use unbounded formatting for user-visible strings

Prefer `vsnprintf()` as used by:

- `oled_printf()`
- `my_printf()`

---

## Required Patterns

### Keep app modules small and focused

One file pair should usually own one concern:

- display
- button behavior
- UART post-processing
- RTC display

### Use task functions as the stable integration seam

The scheduler task table is the project’s main composition point for user-visible periodic logic.

### Defer cross-layer processing out of ISR

If data comes from an interrupt, the app task should process it later using a flag/buffer handoff.

### Favor readable user-facing output

OLED and UART strings should optimize for quick debugging and operator readability, not compact cleverness.

---

## Testing Requirements

Validate app-facing changes through the actual visible behavior:

| Area | Minimum Validation |
|------|--------------------|
| OLED text changes | content fits screen and updates on the expected line |
| Button behavior | click maps to the intended LED or power action |
| UART app behavior | received frame is copied, echoed, and cleared correctly |
| Scheduler changes | new task runs at the expected period and does not starve other tasks |
| RTC / ADC display | displayed values update and formatting remains stable |

Where possible, verify using:

- OLED screen output
- debug UART logs
- LED state changes
- wake/sleep manual test flow

---

## Code Review Checklist

- Is the app code in `USER/App/` instead of leaking into driver/component layers?
- Does the module expose only the public functions or shared buffers it needs?
- Is periodic behavior registered centrally in `scheduler_task[]`?
- Are display and logging buffers bounded?
- Does the task remain lightweight for its configured period?
- If data comes from ISR or DMA, is there a clear handoff boundary?
- Is the visible behavior testable on real hardware?
