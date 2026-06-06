# Backend Development Guidelines

> Low-level firmware infrastructure guidelines for this GD32F470 bare-metal project.

---

## Scope

In this repository, the Trellis `backend` layer does **not** mean a web server.
It maps to hardware-near firmware code:

- `Driver/` board-support, peripheral resource ownership, and reusable device drivers
- `Protocol/` firmware protocol parsing, CRC, and format validation that should stay independent from app tasks
- `Library/` vendor standard peripheral library
- `User/gd32f4xx_it.c` interrupt handlers
- `User/systick.c`, `User/main.c`, and shared runtime infrastructure code
- `HeaderFiles/system_all.h` shared include aggregation

---

## Terminology Mapping

| Template Term | Meaning In This Project |
|---------------|-------------------------|
| Backend | Drivers, interrupts, storage, peripheral initialization, low-level resource management |
| API | Public C functions declared in module headers |
| Database | Persistent storage; formal CIMC build currently uses internal Flash parameter area, not SMARTFS |
| Logging | Debug output through `my_printf()` or `printf` retargeting; formal builds drop logs by default |
| Error response | Return code, contest error frame, debug log, assert trap, or fail-stop loop |

---

## Pre-Development Checklist

- [ ] Read [Directory Structure](./directory-structure.md)
- [ ] Read [Error Handling](./error-handling.md)
- [ ] Read [Logging Guidelines](./logging-guidelines.md)
- [ ] Read [Quality Guidelines](./quality-guidelines.md)
- [ ] If touching Bootloader, App relocation, RS485/USART1 OTA, or Flash partition constants, read [Embedded OTA Guidelines](./embedded-ota-guidelines.md)
- [ ] If touching SPI Flash, SMARTFS remnants, or persistent parameter storage, read [Database Guidelines](./database-guidelines.md)
- [ ] If touching ISR-to-task handoff, DMA buffers, or wakeup flow, also read [`../guides/cross-layer-thinking-guide.md`](../guides/cross-layer-thinking-guide.md)
- [ ] Search existing pin, DMA, IRQ, and buffer-size values before changing them

---

## Guidelines Index

| Guide | Description | Status |
|-------|-------------|--------|
| [Directory Structure](./directory-structure.md) | Layer boundaries, file placement, naming | Project-specific |
| [Database Guidelines](./database-guidelines.md) | Persistent storage conventions and removed SMARTFS legacy notes | Project-specific |
| [Error Handling](./error-handling.md) | Fail-stop, return-code, and ISR safety patterns | Project-specific |
| [Logging Guidelines](./logging-guidelines.md) | Debug logging and formal no-log RS485 rules | Project-specific |
| [Quality Guidelines](./quality-guidelines.md) | Review checklist and forbidden low-level patterns | Project-specific |
| [Embedded OTA Guidelines](./embedded-ota-guidelines.md) | CIMC `0x0501/0x0502/0x0503` OTA, Flash layout, and Bootloader handoff contracts | Project-specific |

---

## Anchor Examples

- `HeaderFiles/system_all.h`: shared include aggregation and layer ordering
- `Driver/USART/bsp_usart.c`: USART1/RS485 pin, DMA, IRQ, and direction-control ownership
- `User/gd32f4xx_it.c`: ISR structure and fail-stop handlers
- `Protocol/cimc_protocol.c`: App-side contest frame parsing and `0x0501` handoff
- `D:\GD32\2026706296_bootloader\Protocol\boot_cimc_protocol.c`: Bootloader-side `0x0502/0x0503` upgrade session

---

**Language**: All guideline documents in this directory should stay in **English**.
