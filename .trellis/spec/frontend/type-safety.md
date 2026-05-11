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

- `UART_APP_DMA_BUFFER_SIZE` in `USER/App/usart_app.h`
- `user_button_t` stays private in `btn_app.c` because it is not cross-module
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
    USER_BUTTON_0 = 0,
    ...
    USER_BUTTON_MAX,
} user_button_t;
```

This is clearer than scattered numeric key IDs.

---

## Validation

There is no schema-validation library.
Runtime validation is manual and explicit.

Required checks include:

- null checks for external pointers when needed
- length bounds before `memcpy()`
- return-code checks after storage operations
- byte-count checks after file read/write

Examples:

- `if (!cfg) return LFS_ERR_INVAL;` in `lfs_storage_init()`
- receive-length clamp in `USART0_IRQHandler()`
- expected-length verification in `sd_fatfs_test()`

---

## Common Patterns

- use `sizeof(buffer)` for buffer bounds
- define lengths as uppercase macros in headers
- cast only when crossing vendor API or register-address boundaries
- keep bitfield/register-layout structs local to the device component that owns them

Good examples:

- `GD30AD3344` bitfield struct in `gd30ad3344.h`
- buffer-size macros in `bsp_usart.h`
- `CONVERT_NUM` and `DAC0_R12DH_ADDRESS` in `bsp_analog.h`

---

## Forbidden Patterns

- avoid magic numbers for buffer lengths when a named macro exists
- avoid unchecked `sprintf()` when `vsnprintf()` or bounded writes are available
- avoid copying raw buffers without validating the actual byte count
- avoid exposing private enums or helper types in headers unless they are part of a stable cross-module contract
