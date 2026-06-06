# Component Guidelines

> How app-facing firmware modules are built in this project.

---

## Overview

An app “component” in this codebase is a small C module that exposes one of:

- an init function
- a periodic task entry
- a formatting helper used by another task

It is not a class or UI framework component.

---

## Component Structure

Typical structure:

1. include its own header
2. declare private enums / constants / static helpers
3. define any local static state
4. expose one or two public entry points

Example from `Function/led_app.c`:

- module-private static state for the system blink phase and hardware cache
- static helpers such as `led_app_build_mask()` and `led_app_refresh()`
- public APIs `led_task()`, `led_app_all_off()`, and `led_app_reset_cache()`

This is the preferred shape for small scheduled app modules.

### Good pattern

```c
static uint8_t led_app_build_mask(void);
static void led_app_refresh(uint8_t led_mask);

void led_task(void);
void led_app_reset_cache(void);
```

---

## Public Interface Conventions

Since there are no props, the equivalent convention is **small public C interfaces**:

- expose only the functions and buffers other modules truly need
- keep callbacks and helper functions private
- declare public buffers in the header only when they are cross-module state by design

Examples:

- `oled_printf()` is public because other app modules use it
- `uart_dma_buffer` and `rx_flag` are declared in `usart_app.h` because ISR code updates them
- contest frame parsing helpers stay private in `Protocol/cimc_protocol.c`

---

## Composition Patterns

Compose user-facing behavior by layering modules:

- driver/component layer provides raw capability
- app layer formats or combines it for display or behavior

Examples:

- `OLED_ShowStr()` is a low-level display primitive
- `oled_printf()` wraps that primitive with formatting
- `oled_task()` renders only team ID and `AutoSample` / `IDLE`
- `rtc_task()` refreshes time cache without writing extra OLED rows

This separation should be preserved.

---

## User-Facing Constraints

For this firmware project, the closest equivalent to accessibility/usability rules is:

- keep OLED lines short enough for the 128x32 display
- keep periodic display refresh rates reasonable
- keep LED semantics fixed to two formal indicators
- avoid blocking USART1/RS485 protocol processing for long unnecessary periods

The current code already follows a lightweight version of this:

- `oled_task()` refreshes the formal two-line display
- `uart_task()` consumes ISR-captured frames and defers CRC/command processing to task context

---

## Common Mistakes

### Putting formatting logic in low-level drivers

Do not push app strings or UI layout concerns into `Driver/OLED/`.

### Exposing private helpers in headers

Private parsing, display-diff, and LED-cache helpers should stay `static` in their `.c` files.

### Making one module own unrelated behaviors

If a task both formats display and changes hardware policy, split the policy from the display concern unless there is a strong reason not to.
