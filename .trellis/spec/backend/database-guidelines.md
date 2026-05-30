# Database Guidelines

> Persistence and storage conventions for this project.

---

## Overview

This firmware project has **no relational database**.
The Trellis `database` guideline maps to persistent storage:

- **SPI Flash + SMARTFS port** for onboard GD25Q16 non-volatile storage

Treat storage changes as interface changes, especially when they affect file names, flash geometry, shell output, or compatibility wrappers.

---

## Storage Patterns

### SMARTFS Port Workflow

The GD25Q16 port now uses `HardWare/GD25QXX/smartfs_port.c/.h`.
It is a bare-metal SMARTFS-style static metadata implementation tailored for this project, not a direct NuttX VFS import.

The port must keep all low-level runtime state in static storage:

- `g_smartfs_image`
- `g_smartfs_temp_image`
- `g_smartfs_sector_buffer`

Do not introduce `malloc()` in this path.

Current GD25Q16 geometry:

| Region | Contract |
|--------|----------|
| SMARTFS managed area | Starts at `0x000000`, length `SMARTFS_FLASH_FS_SIZE` |
| Physical capacity | `SMARTFS_FLASH_TOTAL_SIZE == 2MB` |
| Erase block | `SMARTFS_FLASH_SECTOR_SIZE == 4096` |
| Page program size | `SMARTFS_FLASH_PAGE_SIZE == 256` |
| Metadata area | First 8 sectors, two 4-sector metadata copies |
| Data area | Sectors `8..511` |
| Raw-test reserved area | None; the old final 4KB reserve is removed |

`smart_storage_self_test()` is the preferred boot-time storage smoke test. It must:

- call `smart_storage_init()`
- format the whole SMARTFS area only when no valid SMARTFS metadata copy exists
- create a small directory and file
- read the file back and verify both byte count and content
- verify directory size semantics
- remove the self-test directory before returning

`test_spi_flash()` is a raw-address driver test. Keep it disabled by default.
Because SMARTFS now owns the whole 2MB device, enabling `SPI_FLASH_RAW_TEST_ENABLE`
is destructive: the test erases address `0x000000` and invalidates SMARTFS metadata.

SMARTFS metadata loading must reject corrupt structures before exposing the image to shell helpers:

- validate that active entries use only regular-file or directory types
- validate that each non-root entry has a non-empty name and a live directory parent
- validate that parent links eventually reach the root directory and cannot form a loop
- validate that same-parent active entries do not duplicate names
- validate that file data chains match the file byte length exactly
- clear the loaded flag after any failed metadata commit or post-commit block cleanup so the next command reloads Flash state instead of continuing from an unconfirmed RAM image

Runtime SMARTFS shell helpers should keep behavior explicit:

- use absolute paths at the storage-helper boundary
- keep current-working-directory and relative-path resolution in the UART command layer, not in `smartfs_port.c`
- do not auto-format on runtime read/write/list/stat commands
- if parent directories do not exist, `write`, `touch`, and `mkdir` should fail clearly instead of silently creating multi-level parents
- directory listing helpers should not synthesize `.` or `..`

### Scenario: SMARTFS UART Shell Directory Size Semantics And Safe Recursion

#### 1. Scope / Trigger

- Trigger: editing `Function/usart_app.c` shell commands such as `ls`, `stat`, or `rm`.
- Trigger: editing `HardWare/GD25QXX/smartfs_port.c` path-info, directory traversal, append, overwrite, delete, or block-map helpers.
- Trigger: any change that makes UART shell output depend on recursive directory inspection or storage-helper recursion.

#### 2. Signatures

Current shell-facing storage contracts:

```c
typedef struct
{
    uint8_t exists;
    uint16_t type;
    uint32_t size;
} smart_storage_path_info_t;

typedef struct
{
    smart_storage_entry_info_t info;
    uint32_t display_size;
} smart_storage_dir_entry_t;

int smart_storage_get_path_info(const char *path, smart_storage_path_info_t *info);
int smart_storage_list_dir(const char *path,
                           smart_storage_dir_list_callback_t callback,
                           void *context,
                           uint32_t *out_count);
int smart_storage_remove_path(const char *path);
int smart_storage_write_file(const char *path, const uint8_t *data, uint32_t length);
int smart_storage_append_file(const char *path, const uint8_t *data, uint32_t length);
```

Current UART shell output contracts:

```text
SMARTFS: LS /path
file    <size>  <name>
dir     <size>  <name>
SMARTFS: LS done count=<n>

SMARTFS: STAT path=/path type=file size=<bytes>
SMARTFS: STAT path=/path type=dir size=<bytes>

write <file> <text>      -> overwrite
write -a <file> <text>   -> append

RTC: YYYY-MM-DD HH:MM:SS
settime YYYY-MM-DD HH:MM:SS -> RTC: SET OK YYYY-MM-DD HH:MM:SS
```

#### 3. Contracts

- `ls` numeric column must keep a single semantic: **size in bytes**.
- For regular files, `size` / `display_size` must equal the file byte length from SMARTFS metadata.
- For directories, `size` / `display_size` must equal the **recursive sum of all descendant file content bytes**.
- Do not overload the `ls` numeric column with entry count. Entry count may be added later only under a separate field or command.
- `smart_storage_get_path_info()` is the shell-safe API. It may enrich directory size for display after resolving the target path.
- If a path query is blocked by a missing intermediate directory, `smart_storage_get_path_info()` returns `SMART_STORAGE_ERR_OK` with `exists=0`; create/write helpers must still return `SMART_STORAGE_ERR_NOENT` for the same condition.
- Recursive size calculation must use a dedicated helper such as `prv_smartfs_calculate_entry_size()`.
- Recursive delete should first update and commit metadata, then release old file data chains, then commit the cleaned block map.
- `write` without `-a` must keep overwrite semantics.
- `write -a` must keep append semantics by preserving existing content and writing new bytes at file end.
- UART command-layer append verification should read back the file and confirm both:
  - final file length equals `old_length + appended_length`
  - the file tail matches the appended text exactly

#### 4. Validation & Error Matrix

| Observation | Meaning | Required Action |
|-------------|---------|-----------------|
| Root `ls` shows `dir 0 app` while `/app` contains files | Directory column is hardcoded zero or not recursively calculated | Compute recursive descendant file bytes for directories |
| Root `ls` shows `dir 2 app` while files inside sum to `694` bytes | Directory column is using child-count semantics instead of byte-size semantics | Switch shell contract back to byte-size semantics |
| `write -a` fails on a file near the UART buffer size | Append path may still depend on an App-layer buffer | Keep append implementation in `smartfs_port.c` sector-buffer based |
| File `stat` is correct but directory `stat` differs from `ls` on the same path | Shell output contracts drifted between APIs | Unify both to the same recursive byte-size helper |
| `SPI_FLASH_RAW_TEST_ENABLE=1` followed by missing SMARTFS metadata | Expected destructive raw test behavior | Reformat SMARTFS or disable raw test before normal use |

#### 5. Good / Base / Bad Cases

| Case | Expected Result |
|------|-----------------|
| Good | `/app` contains `test.cls=0` and `test.c=694`; root `ls` shows `dir 694 app`; `stat /app` shows `type=dir size=694` |
| Base | Empty directory shows `dir 0 emptydir`; this is valid because recursive descendant file bytes are zero |
| Bad | Directory output shows `0` only because the code hardcodes directory size to zero |
| Bad | Directory output shows `2` because the code reused child-count instead of byte-size semantics |
| Bad | Runtime write path calls `smart_storage_format()` after a normal command fails |

#### 6. Tests Required

- Boot and confirm `smart_storage_self_test()` passes or formats once then passes.
- Create one directory with at least two files whose sizes are easy to sum manually.
- Assert `ls /parent` prints the directory line with the summed descendant file bytes.
- Assert `stat /parent` prints the same byte total as `ls`.
- Add one empty subdirectory and verify the parent size does not increase unless files are created inside it.
- Run `ls`, `stat`, `cat`, and `rm` in sequence after boot and assert no `ASSERT:` log appears.
- Build with AC6 and confirm there are no implicit-function-declaration errors; static helper declarations must be explicit or ordered before use.

#### 7. Wrong vs Correct

##### Wrong

```c
/* Wrong: directory display semantics drift to child count. */
if (SMART_STORAGE_TYPE_DIR == entry->type) {
    info->size = prv_smartfs_count_direct_children(entry_index);
}

/* Wrong: runtime commands silently wipe user data after a normal lookup failure. */
if (SMART_STORAGE_ERR_OK != err) {
    smart_storage_format();
}
```

##### Correct

```c
/* Correct: directory size remains recursive content bytes. */
if (SMART_STORAGE_TYPE_DIR == info->type) {
    err = prv_smartfs_calculate_entry_size(entry_index, &info->size);
}

/* Correct: startup init is the only automatic format path. */
err = smart_storage_init();
```

### Readback Verification

When a storage path is used as a demo, smoke test, or migration safety check, verify both:

- status code
- returned byte count or content match

Example:

```c
if ((SMART_STORAGE_ERR_OK == err) &&
    (read_length == expected_len) &&
    (0 == memcmp(read_buffer, expected_data, expected_len))) {
    /* verified */
}
```

---

## Migrations

There is no migration framework.
When storage format changes are needed:

- version the file name, path, or payload format explicitly
- keep compatibility wrappers when an old API name is still referenced
- document flash geometry changes in the header macros
- assume old LittleFS contents are incompatible with the current SMARTFS format unless a migration tool is explicitly implemented

---

## Naming Conventions

- Storage geometry macros are uppercase and explicit, such as `SMARTFS_FLASH_TOTAL_SIZE`
- App-facing SMARTFS helpers use the prefix `smart_storage_`
- Compatibility aliases keep the old prefix only when required for transition safety

Examples:

- `SMARTFS_FLASH_SECTOR_SIZE`
- `smart_storage_self_test()`
- `smart_storage_write_file()`

---

## Common Mistakes

### Accessing the file system before initialization

Do not call SMARTFS read/write/list helpers before `smart_storage_init()` has succeeded during startup.
Runtime commands should load existing metadata, but they must not implicitly format user data.

### Writing the whole fixed buffer instead of the valid payload

Good:

```c
smart_storage_write_file(path, data, valid_length);
```

Bad:

```c
smart_storage_write_file(path, data, sizeof(data));
```

### Introducing heap allocation in low-level storage paths

Follow the static-buffer pattern from `smartfs_port.c`.
This project does not use `malloc()` for its storage configuration path.

### Re-enabling destructive raw Flash tests during normal SMARTFS use

`SPI_FLASH_RAW_TEST_ENABLE` must stay disabled for normal firmware.
With full-device SMARTFS, raw test erase/write operations are destructive by design.
