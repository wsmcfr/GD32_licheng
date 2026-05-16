# Backend Development Guidelines

> Low-level firmware infrastructure guidelines for this GD32F470 bare-metal project.

---

## Scope

In this repository, the Trellis `backend` layer does **not** mean a web server.
It maps to hardware-near firmware code:

- `USER/Driver/` board-support and peripheral resource ownership
- `USER/Component/` reusable device and protocol implementations
- `USER/gd32f4xx_it.c` interrupt handlers
- `USER/systick.c`, `USER/main.c`, and shared infrastructure code

---

## Terminology Mapping

| Template Term | Meaning In This Project |
|---------------|-------------------------|
| Backend | Drivers, interrupts, storage, peripheral initialization, low-level resource management |
| API | Public C functions declared in module headers |
| Database | Persistent storage on SD/FatFs or SPI Flash/LittleFS |
| Logging | Debug UART output through `my_printf()` or `printf` redirection |
| Error response | Return code, debug log, assert trap, or fail-stop loop |

---

## Pre-Development Checklist

- [ ] Read [Directory Structure](./directory-structure.md)
- [ ] Read [Error Handling](./error-handling.md)
- [ ] Read [Logging Guidelines](./logging-guidelines.md)
- [ ] Read [Quality Guidelines](./quality-guidelines.md)
- [ ] If touching BootLoader, App relocation, RS485/USART1 OTA, or Flash partition constants, read [Embedded OTA Guidelines](./embedded-ota-guidelines.md)
- [ ] If touching SD card, SPI Flash, or LittleFS, read [Database Guidelines](./database-guidelines.md)
- [ ] If touching ISR-to-task handoff, DMA buffers, or wakeup flow, also read [`../guides/cross-layer-thinking-guide.md`](../guides/cross-layer-thinking-guide.md)
- [ ] Search existing pin, DMA, IRQ, and buffer-size values before changing them

---

## Guidelines Index

| Guide | Description | Status |
|-------|-------------|--------|
| [Directory Structure](./directory-structure.md) | Layer boundaries, file placement, naming | Project-specific |
| [Database Guidelines](./database-guidelines.md) | SD/FatFs and SPI Flash/LittleFS persistence conventions | Project-specific |
| [Error Handling](./error-handling.md) | Fail-stop, return-code, and ISR safety patterns | Project-specific |
| [Logging Guidelines](./logging-guidelines.md) | Debug UART logging conventions | Project-specific |
| [Quality Guidelines](./quality-guidelines.md) | Review checklist and forbidden low-level patterns | Project-specific |
| [Embedded OTA Guidelines](./embedded-ota-guidelines.md) | RS485/USART1 App-side OTA package, Flash layout, and BootLoader handoff contracts | Project-specific |

---

## Anchor Examples

- `USER/system_all.h`: shared include aggregation and layer ordering
- `USER/Driver/bsp_usart.c`: typical peripheral/DMA/IRQ initialization style
- `USER/App/sd_app.c`: storage status checking, logging, and compatibility wrappers
- `USER/gd32f4xx_it.c`: ISR structure and fail-stop handlers
- `USER/App/uart_ota_app.c`: RS485/USART1 OTA packet parsing and BootLoader parameter handoff
- `tools/make_uart_ota_packet.py`: PC-side `.uota` packet generator

---

**Language**: All guideline documents in this directory should stay in **English**.
