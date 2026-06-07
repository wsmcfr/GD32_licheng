# Type Safety

> Type and contract conventions for app-visible C code.

---

## Overview

Type safety in this project comes from:

- fixed-width integer types
- explicit enums for logical identities
- macros for hardware constants
- small public header contracts
- manual runtime bounds checks where raw data crosses layers

---

## Type Organization

### Public shared types live in headers

Put public enums, macros, and `extern` declarations in the matching module header.

Examples:

- `UART_APP_DMA_BUFFER_SIZE` in `Function/usart_app.h`
- `GD30AD3344_Channel_TypeDef` is public in `gd30ad3344.h`

### Use fixed-width integers for hardware-facing data

Prefer:

- `uint8_t`
- `uint16_t`
- `uint32_t`

over plain `int` when the width matters for:

- DMA buffers
- register payloads
- storage sizes
- pin or peripheral values

### Use enums for semantic IDs

Good example:

```c
typedef enum
{
    CIMC_RESULT_OK = 0,
    ...
    CIMC_RESULT_MAX,
} cimc_result_t;
```

This is clearer than scattered numeric result IDs.

---

## Validation

There is no schema-validation library.
Runtime validation is manual and explicit.

Required checks include:

- null checks for external pointers when needed
- length bounds before `memcpy()`
- return-code checks after Flash, sensor, or Bootloader handoff operations
- byte-count checks before protocol frame parsing and ISR-to-task copying

Examples:

- receive-length clamp in `uart_task()` after DMA is stopped
- expected-length verification in `proto_rx()`
- 12-bit range validation before applying DAC command `0x0301`

---

## Common Patterns

- use `sizeof(buffer)` for buffer bounds
- define lengths as uppercase macros in headers
- cast only when crossing vendor API or register-address boundaries
- keep bitfield/register-layout structs local to the device component that owns them

Good examples:

- `GD30AD3344` bitfield struct in `gd30ad3344.h`
- buffer-size macros in `bsp_usart.h`
- `CONVERT_NUM` and `DAC1_PIN` in `bsp_analog.h`

---

## Forbidden Patterns

- avoid magic numbers for buffer lengths when a named macro exists
- avoid unchecked `sprintf()` when `vsnprintf()` or bounded writes are available
- avoid copying raw buffers without validating the actual byte count
- avoid exposing private enums or helper types in headers unless they are part of a stable cross-module contract
