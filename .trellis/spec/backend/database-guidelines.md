# Database Guidelines

> Persistence and storage conventions for this project.

---

## Overview

This firmware project has **no relational database**.
The Trellis `database` guideline maps to persistent storage:

- **SDIO + FatFs** for removable file-based storage
- **SPI Flash + LittleFS port** for onboard non-volatile storage experiments

Treat storage changes as interface changes, especially when they affect file names, flash geometry, or compatibility wrappers.

---

## Storage Patterns

### FatFs Workflow

The common pattern is:

1. initialize the physical drive
2. mount the file system
3. open/create the file
4. write or read only the valid byte count
5. close the file
6. verify the result when the code is a demo or self-test path

Example from `USER/App/sd_app.c`:

```c
stat = disk_initialize(0);
result = f_mount(0, &fs);
result = f_open(&fdst, "0:/FATFS.TXT", FA_CREATE_ALWAYS | FA_WRITE);
result = f_write(&fdst, filebuffer, (UINT)strlen((char *)filebuffer), &bw);
f_close(&fdst);
```

### LittleFS Port Workflow

The LittleFS port uses static buffers and callback registration in `lfs_storage_init()`:

```c
cfg->read = lfs_deskio_read;
cfg->prog = lfs_deskio_prog;
cfg->erase = lfs_deskio_erase;
cfg->sync = lfs_deskio_sync;
```

This project avoids heap allocation in storage configuration:

- `lfs_read_buffer`
- `lfs_prog_buffer`
- `lfs_lookahead_buffer`

are all static arrays in `USER/Component/gd25qxx/lfs_port.c`.

The GD25QXX LittleFS layout reserves the final erase block for optional raw
Flash smoke tests. The filesystem must only expose `LFS_FLASH_FS_SIZE` through
`cfg->block_count`; do not let LittleFS allocate the reserved block.

| Region | Contract |
|--------|----------|
| LittleFS managed area | Starts at `0x000000`, length `LFS_FLASH_FS_SIZE` |
| Raw-test reserved area | Starts at `LFS_FLASH_RAW_TEST_START_ADDR`, length `LFS_FLASH_RAW_TEST_RESERVED_SIZE` |

`lfs_storage_self_test()` is the preferred boot-time storage smoke test. It must:

- mount the filesystem first
- format only after mount fails
- write and read back a small file
- check both byte count and content
- unmount before returning

`test_spi_flash()` is a raw-address driver test. Keep it disabled by default and
ensure it only erases the raw-test reserved area. It must never erase address
`0x000000` while LittleFS is used, because that can destroy the LittleFS
superblock and metadata.

Runtime LittleFS shell helpers should keep filesystem behavior explicit:

- use absolute paths at the storage-helper boundary
- keep current-working-directory and relative-path resolution in the UART command layer, not in `lfs_port.c`
- do not auto-format on runtime read/write/list/stat commands
- if parent directories do not exist, `write`, `touch`, and `mkdir` should fail clearly instead of silently creating multi-level parents
- directory listing helpers should filter `.` and `..` before echoing results to the shell

### Scenario: LittleFS UART Shell Directory Size Semantics And Safe Recursion

#### 1. Scope / Trigger

- Trigger: editing `USER/App/usart_app.c` shell commands such as `ls`, `stat`, `rm`, or editing `USER/Component/gd25qxx/lfs_port.c` path-info / directory traversal helpers.
- Trigger: any change that makes UART shell output depend on recursive directory inspection or that adds new storage-helper recursion.

#### 2. Signatures

Current shell-facing storage contracts:

```c
typedef struct
{
    uint8_t exists;
    uint16_t type;
    uint32_t size;
} lfs_storage_path_info_t;

typedef struct
{
    struct lfs_info info;
    uint32_t display_size;
} lfs_storage_dir_entry_t;

int lfs_storage_get_path_info(const char *path, lfs_storage_path_info_t *info);
int lfs_storage_list_dir(const char *path,
                         lfs_storage_dir_list_callback_t callback,
                         void *context,
                         uint32_t *out_count);
int lfs_storage_remove_path(const char *path);
```

Current UART shell output contracts:

```text
LFS: LS /path
file    <size>  <name>
dir     <size>  <name>
LFS: LS done count=<n>

LFS: STAT path=/path type=file size=<bytes>
LFS: STAT path=/path type=dir size=<bytes>
```

#### 3. Contracts

- `ls` numeric column must keep a single semantic: **size in bytes**.
- For regular files, `size` / `display_size` must equal the file byte length from LittleFS metadata.
- For directories, `size` / `display_size` must equal the **recursive sum of all descendant file content bytes**.
- Do not overload the `ls` numeric column with entry count. Entry count may be added later only under a separate field or command.
- `lfs_storage_get_path_info()` is the shell-safe API. It may enrich directory size for display, but only after a raw non-recursive path-type lookup succeeds.
- Recursive size calculation must use a dedicated helper such as `prv_lfs_calculate_path_size_mounted()`.
- Recursive size calculation must depend on a **raw path-info helper** such as `prv_lfs_get_path_info_raw_mounted()` that only returns existence/type/raw `lfs_stat()` size.
- Recursive helpers must never call an enriched helper that itself re-enters recursive size calculation; otherwise directory queries can recurse into themselves and corrupt LittleFS traversal state.
- Recursive delete helpers may use the same raw path-info helper to distinguish file vs directory before descending.

#### 4. Validation & Error Matrix

| Observation | Meaning | Required Action |
|-------------|---------|-----------------|
| Root `ls` shows `dir 0 app` while `/app` contains files | directory column is using raw LittleFS directory size or hardcoded zero | compute recursive descendant file bytes for directories |
| Root `ls` shows `dir 2 app` while files inside sum to `694` bytes | directory column is using child-count semantics instead of byte-size semantics | switch shell contract back to byte-size semantics |
| `ls` or `stat` hangs or hits `ASSERT: block != ((lfs_block_t) - 1)` after a command | recursive helper re-entered high-level path-info/query logic and corrupted directory traversal state | split raw lookup from enriched recursive display logic |
| File `stat` is correct but directory `stat` differs from `ls` on the same path | shell output contracts drifted between APIs | unify both to the same recursive byte-size helper |

#### 5. Good / Base / Bad Cases

| Case | Expected Result |
|------|-----------------|
| Good | `/app` contains `test.cls=0` and `test.c=694`; root `ls` shows `dir 694 app`; `stat /app` shows `type=dir size=694` |
| Base | Empty directory shows `dir 0 emptydir`; this is valid because recursive descendant file bytes are zero |
| Bad | Directory output shows `0` only because the code hardcodes directory size to zero |
| Bad | Directory output shows `2` because the code reused child-count instead of byte-size semantics |
| Bad | Recursive size helper calls enriched `get_path_info`, causing nested recursion and LittleFS assert traps during shell commands |

#### 6. Tests Required

- Create one directory with at least two files whose sizes are easy to sum manually.
- Assert `ls /parent` prints the directory line with the summed descendant file bytes.
- Assert `stat /parent` prints the same byte total as `ls`.
- Add one empty subdirectory and verify the parent size does not increase unless files are created inside it.
- Run `ls`, `stat`, `cat`, and `rm` in sequence after boot and assert no `ASSERT:` log appears.
- If changing recursive helpers, build with AC6 and confirm there are no implicit-function-declaration errors; static helper declarations must be explicit.

#### 7. Wrong vs Correct

##### Wrong

```c
/* Wrong: directory display semantics drift to child count. */
if (LFS_TYPE_DIR == lfs_info.type) {
    err = prv_lfs_count_dir_entries_mounted(lfs, path, &info->direct_child_count);
}

/* Wrong: recursive size helper re-enters enriched path info and can recurse forever. */
err = prv_lfs_get_path_info_mounted(lfs, normalized_path, &path_info);
```

##### Correct

```c
/* Correct: raw lookup only returns existence/type/raw metadata. */
err = prv_lfs_get_path_info_raw_mounted(lfs, normalized_path, &path_info);

/* Correct: enriched helper computes directory display size separately. */
if (LFS_TYPE_DIR == info->type) {
    err = prv_lfs_calculate_path_size_mounted(lfs, path, &info->size);
}
```

### Readback Verification

When a storage path is used as a demo, smoke test, or migration safety check, verify both:

- status code
- returned byte count or content match

Example:

```c
if ((FR_OK == result) && (br == expected_len) &&
    (SUCCESS == memory_compare(buffer, filebuffer, (uint16_t)expected_len)))
```

---

## Migrations

There is no migration framework.
When storage format changes are needed:

- version the file name, path, or payload format explicitly
- keep compatibility wrappers when an old API name is still referenced
- document flash geometry changes in the header macros

Existing compatibility example in `USER/App/sd_app.c`:

```c
void sd_lfs_init(void)
{
    sd_fatfs_init();
}
```

This wrapper keeps old call sites working while the implementation has already moved to FatFs naming.

---

## Naming Conventions

- FatFs paths use drive-prefixed absolute paths such as `"0:/FATFS.TXT"`
- Long demo filenames use descriptive underscore-separated names
- Storage geometry macros are uppercase and explicit, such as `LFS_FLASH_TOTAL_SIZE`
- App-facing storage helpers use the prefix `sd_fatfs_`
- Compatibility aliases keep the old prefix only when required for transition safety

Examples:

- `SD_FATFS_DEMO_ENABLE`
- `LFS_FLASH_SECTOR_SIZE`
- `sd_fatfs_long_name_test()`
- `sd_lfs_test()`

---

## Common Mistakes

### Accessing the file system before mount

Do not call `f_open()` before `disk_initialize()` and `f_mount()`.
The project treats mount as a required explicit step.

### Writing the whole fixed buffer instead of the valid payload

Good:

```c
f_write(&fdst, filebuffer, (UINT)strlen((char *)filebuffer), &bw);
```

Bad:

```c
f_write(&fdst, filebuffer, sizeof(filebuffer), &bw);
```

### Assuming long filename support is enabled

`sd_fatfs_long_name_test()` explicitly checks for `FR_INVALID_NAME`.
If long filenames matter, verify `ffconf.h` instead of assuming the configuration.

### Introducing heap allocation in low-level storage paths

Follow the static-buffer pattern from `lfs_port.c`.
This project does not use `malloc()` for its storage configuration path.

LittleFS file handles also need explicit static file buffers when opened through
`lfs_file_opencfg()`. Calling `lfs_file_open()` without a file buffer can make
LittleFS allocate a per-file cache through `lfs_malloc()`, which is not allowed
in this project's low-level storage paths.
