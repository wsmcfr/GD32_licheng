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
