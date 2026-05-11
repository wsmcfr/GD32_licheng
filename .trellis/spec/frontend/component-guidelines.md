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

Example from `USER/App/btn_app.c`:

- private enum `user_button_t`
- shared static params `defaul_ebtn_param`
- static callbacks `prv_btn_get_state()` and `prv_btn_event()`
- public APIs `app_btn_init()` and `btn_task()`

This is the preferred shape for event-driven app modules.

### Good pattern

```c
static uint8_t prv_btn_get_state(struct ebtn_btn *btn);
static void prv_btn_event(struct ebtn_btn *btn, ebtn_evt_t evt);

void app_btn_init(void);
void btn_task(void);
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
- `memory_compare()` stays private in `sd_app.c`

---

## Composition Patterns

Compose user-facing behavior by layering modules:

- driver/component layer provides raw capability
- app layer formats or combines it for display or behavior

Examples:

- `OLED_ShowStr()` is a low-level display primitive
- `oled_printf()` wraps that primitive with formatting
- `rtc_task()` uses `oled_printf()` to render time

This separation should be preserved.

---

## User-Facing Constraints

For this firmware project, the closest equivalent to accessibility/usability rules is:

- keep OLED lines short enough for the 128x32 display
- keep periodic display refresh rates reasonable
- ensure button actions are predictable and stable
- avoid blocking user feedback paths for long unnecessary periods

The current code already follows a lightweight version of this:

- `oled_task()` refreshes summary information every scheduler cycle
- `btn_task()` uses `ebtn_process()` for debounced event handling

---

## Common Mistakes

### Putting formatting logic in low-level drivers

Do not push app strings or UI layout concerns into `USER/Component/oled/`.

### Exposing private callbacks in headers

The `prv_*` pattern in `btn_app.c` is correct. Keep callback glue private.

### Making one module own unrelated behaviors

If a task both formats display and changes hardware policy, split the policy from the display concern unless there is a strong reason not to.
