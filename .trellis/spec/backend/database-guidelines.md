# Database Guidelines

> Persistence and storage conventions for the formal CIMC firmware build.

---

## Overview

This firmware project has no relational database. The `database` layer maps to
non-volatile storage contracts.

Formal CIMC builds use the internal Flash parameter area at
`0x08010000 ~ 0x08010FFF` for Bootloader handoff data and any future contest
parameters that need persistence. External GD25QXX Flash, SMARTFS, and littlefs
are not compiled into the formal App target.

---

## Formal Storage Contract

| Storage | Formal Status | Contract |
|---------|---------------|----------|
| Internal Flash parameter area | Active | Shared by App and Bootloader; field offsets must stay compatible |
| App run area | Active | `0x08011000 ~ 0x08030FFF`, final image location |
| App backup area | Active | `0x08031000 ~ 0x08050FFF`, rollback source |
| App download buffer | Active | `0x08051000 ~ 0x08070FFF`, Bootloader stores received payload |
| GD25QXX raw driver | Removed from source tree | Reintroduce only after a new requirement review, and only as minimal raw read/write |
| SMARTFS / littlefs | Removed from source tree | Do not reintroduce file-system shell or boot-time self-test for CIMC |

---

## Parameter Area Rules

- App and Bootloader must agree on every field offset in the first 4KB parameter
  page.
- App `0x0501` must write only the Bootloader waiting-window request fields:
  `magicWord`, `updateFlag`, and `updateStatus=0x02`.
- Bootloader owns the `0x0502/0x0503` receive/copy lifecycle after reset.
- Any new persistent contest parameter must be added only after checking the
  current Bootloader structure and unused/reserved bytes.
- Do not store large logs, file-system metadata, or arbitrary user files in the
  4KB parameter page.

---

## Removed File-System Contract

Do not add these files back to `project/2026706296.uvprojx` for the formal target:

| Removed Entry | Reason |
|---------------|--------|
| `Driver/GD25QXX/smartfs_port.c` | Contest does not need directory or file operations |
| `Driver/GD25QXX/lfs.c` / `lfs_util.c` | File system adds code size and startup risk |
| `Function/usart_app.c` SMARTFS shell commands | USART0 shell is not a scoring interface |
| `SMART_STORAGE_BOOT_SELF_TEST_ENABLE` | Boot-time format/self-test changes startup timing |

The current formal tree physically removes the GD25QXX, button, power-demo, and
old App-side OTA source files; do not recreate them for contest work unless the
architecture is intentionally changed again.

---

## Validation

Before committing storage-related work:

```powershell
rg -n "smartfs_port\.c|lfs\.c|lfs_util\.c|bsp_key\.c|bsp_power\.c|uart_ota_app\.c|ota_image_protocol\.c" project\2026706296.uvprojx
rg -n "Project_ota\.bin|pack_ota_image|SMARTFS|littlefs|USART0" -S -g "*.md" .
```

Expected result:

- The Keil project must not compile removed file-system or USART0 shell sources.
- User-facing docs may mention old terms only as explicitly removed legacy
  behavior.
- App and Bootloader builds still report `0 Error(s), 0 Warning(s)`.
