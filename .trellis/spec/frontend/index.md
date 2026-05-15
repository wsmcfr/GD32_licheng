# Frontend Development Guidelines

> Application-task and user-facing firmware behavior guidelines for this GD32F470 project.

---

## Scope

In this repository, the Trellis `frontend` layer does **not** mean a browser UI.
It maps to firmware code that directly shapes what the user sees or triggers:

- `USER/App/` scheduled application tasks
- OLED text rendering and display composition
- button event behavior
- app-visible state derived from drivers or interrupts

---

## Terminology Mapping

| Template Term | Meaning In This Project |
|---------------|-------------------------|
| Frontend | Application tasks, OLED display logic, button interaction, demo behavior |
| Component | App-facing module such as `oled_app`, `btn_app`, `rtc_app`, `led_app` |
| Hook | Repeated event/time-driven glue pattern, usually callback or periodic task logic |
| State management | Global/static buffers, flags, task-owned state arrays, and ISR-to-task handoff |
| Type safety | Explicit C types, enums, macros, and shared-header contracts |

---

## Pre-Development Checklist

- [ ] Read [Directory Structure](./directory-structure.md)
- [ ] Read [Component Guidelines](./component-guidelines.md)
- [ ] Read [State Management](./state-management.md)
- [ ] Read [Type Safety](./type-safety.md)
- [ ] Read [Quality Guidelines](./quality-guidelines.md)
- [ ] If adding timer-driven or callback-driven logic, read [Hook Guidelines](./hook-guidelines.md)
- [ ] If the app task consumes data from ISR, DMA, or storage, also read [`../guides/cross-layer-thinking-guide.md`](../guides/cross-layer-thinking-guide.md)

---

## Guidelines Index

| Guide | Description | Status |
|-------|-------------|--------|
| [Directory Structure](./directory-structure.md) | App task placement and task-module boundaries | Project-specific |
| [Component Guidelines](./component-guidelines.md) | How app tasks and user-visible modules are structured | Project-specific |
| [Hook Guidelines](./hook-guidelines.md) | Callback and periodic processing patterns | Project-specific |
| [State Management](./state-management.md) | Flags, buffers, and shared state ownership | Project-specific |
| [Type Safety](./type-safety.md) | Enums, macros, fixed-width types, and header contracts | Project-specific |
| [Quality Guidelines](./quality-guidelines.md) | Review and validation rules for app-visible behavior | Project-specific |

---

## Anchor Examples

- `USER/App/scheduler.c`: central task registration and startup sequencing
- `USER/App/oled_app.c`: user-facing text composition
- `USER/App/btn_app.c`: periodic key polling and action mapping from physical inputs to behavior
- `USER/App/usart_app.c`: app-level processing of ISR-delivered UART data

---

**Language**: All guideline documents in this directory should stay in **English**.
