# Quality Guidelines

> Code quality standards for low-level firmware work in this project.

---

## Overview

This repository does not currently have automated linting or unit-test infrastructure for the firmware C code.
Quality is maintained through:

- strong module boundaries
- explicit initialization and ownership
- defensive checks around buffers and return codes
- hardware-backed smoke tests
- readable comments and stable naming

---

## Forbidden Patterns

### Do not put heavy work in interrupt handlers

Bad patterns include:

- formatting long strings
- scanning storage
- doing business logic in the ISR

Reference: `USER/gd32f4xx_it.c` keeps ISR work limited to flag clear, bounded copy, DMA re-arm, and exit.

### Do not change hardware constants without impact search

Before changing:

- pin macros
- DMA channel mappings
- baud rates
- buffer sizes

search the repository for all dependent usages.

### Do not use dynamic allocation in low-level storage or hot runtime paths

The existing storage port uses static buffers in `USER/Component/gd25qxx/lfs_port.c`.
Follow that pattern unless there is a very strong reason to introduce heap use.

### Do not expose private helpers through headers

Keep callback glue and local helpers `static` in the `.c` file.
Examples:

- `memory_compare()` in `USER/App/sd_app.c`
- `bsp_usart_disable_for_deepsleep()` in `USER/Driver/bsp_power.c`

### Do not ignore valid-length tracking

Always respect the actual data length instead of a whole fixed buffer.
This matters for both UART frames and file writes.

---

## Required Patterns

### One public header per module

Each module should have:

- a `.h` with macros, `extern` declarations, and public function prototypes
- a `.c` with private helpers and implementation details

### Explicit initialization order

System bring-up is intentionally serialized in `USER/App/scheduler.c::system_init()`.
If a new peripheral has dependencies, place it in the correct boot order and log the stage if useful.

### Fixed-width integer types for hardware-facing data

Use `uint8_t`, `uint16_t`, `uint32_t`, and explicit buffer-size macros for data that crosses registers, DMA, or storage boundaries.

### Bounded buffer operations

Good existing examples:

- `vsnprintf(buffer, sizeof(buffer), ...)`
- UART copy length clamp in `USART0_IRQHandler()`
- file readback length check in `sd_fatfs_test()`

### Backward-compatible wrappers when renaming public interfaces

The `sd_lfs_*` wrappers in `USER/App/sd_app.c` show the expected compatibility style when an external name still exists in call sites or old documentation.

---

## Testing Requirements

For backend changes, prefer hardware-backed smoke tests over unverified assumptions.

Minimum expected checks depend on the changed area:

| Area | Minimum Validation |
|------|--------------------|
| UART / DMA / IRQ | boot log still prints, receive path still echoes or frames correctly |
| Storage | init, open/write/read, and readback verification still pass |
| OLED / I2C / SPI | boot sequence completes and dependent app task still runs |
| Power / wakeup | enter sleep, wake with key, and re-init order still works |
| ADC / DAC / RTC | displayed values or serial diagnostics still update |

Use the existing self-test paths where possible:

- `test_spi_flash()`
- `sd_fatfs_test()`
- boot logs in `system_init()`

---

## Code Review Checklist

- Is the file placed in the correct layer: `Driver`, `Component`, `App`, or infrastructure?
- Are pin macros, DMA resources, and shared buffers declared in the header that owns them?
- Are private helpers `static`?
- Are return codes checked immediately?
- Are copy lengths and formatted buffers bounded?
- Does any ISR stay minimal and defer work to app context?
- If an API name changed, is backward compatibility needed?
- Is the new behavior covered by a concrete hardware or smoke-test plan?
