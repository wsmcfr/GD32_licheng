#include "mcu_cmic_gd32f470vet6.h"

FATFS fs;
FIL fdst;
uint16_t i = 0, count, result = 0;
UINT br, bw;

sd_card_info_struct sd_cardinfo;

BYTE buffer[128];
BYTE filebuffer[128];

ErrStatus memory_compare(uint8_t* src, uint8_t* dst, uint16_t length)
{
    while(length --){
        if(*src++ != *dst++)
            return ERROR;
    }
    return SUCCESS;
}

void sd_fatfs_init(void)
{
    /* Enable SDIO IRQ with highest priority in this project setup. */
    nvic_irq_enable(SDIO_IRQn, 0, 0);
}

/**
 * @brief Print SD card information through debug UART.
 */
void card_info_get(void)
{
    sd_card_info_struct sd_cardinfo;
    sd_error_enum status;
    uint32_t block_count, block_size;

    /* Read SD card CID/CSD information. */
    status = sd_card_information_get(&sd_cardinfo);

    if(SD_OK == status)
    {
        my_printf(DEBUG_USART, "\r\n*** SD Card Info ***\r\n");

        /* Print card type. */
        switch(sd_cardinfo.card_type)
        {
            case SDIO_STD_CAPACITY_SD_CARD_V1_1:
                my_printf(DEBUG_USART, "Card Type: Standard Capacity SD Card V1.1\r\n");
                break;
            case SDIO_STD_CAPACITY_SD_CARD_V2_0:
                my_printf(DEBUG_USART, "Card Type: Standard Capacity SD Card V2.0\r\n");
                break;
            case SDIO_HIGH_CAPACITY_SD_CARD:
                my_printf(DEBUG_USART, "Card Type: High Capacity SD Card\r\n");
                break;
            case SDIO_MULTIMEDIA_CARD:
                my_printf(DEBUG_USART, "Card Type: Multimedia Card\r\n");
                break;
            case SDIO_HIGH_CAPACITY_MULTIMEDIA_CARD:
                my_printf(DEBUG_USART, "Card Type: High Capacity Multimedia Card\r\n");
                break;
            case SDIO_HIGH_SPEED_MULTIMEDIA_CARD:
                my_printf(DEBUG_USART, "Card Type: High Speed Multimedia Card\r\n");
                break;
            default:
                my_printf(DEBUG_USART, "Card Type: Unknown\r\n");
                break;
        }

        /* Print capacity and geometry. */
        block_count = (sd_cardinfo.card_csd.c_size + 1) * 1024;
        block_size = 512;
        my_printf(DEBUG_USART,"\r\n## Device size is %dKB (%.2fGB)##", sd_card_capacity_get(), sd_card_capacity_get() / 1024.0f / 1024.0f);
        my_printf(DEBUG_USART,"\r\n## Block size is %dB ##", block_size);
        my_printf(DEBUG_USART,"\r\n## Block count is %d ##", block_count);

        /* Print manufacturer and OEM IDs. */
        my_printf(DEBUG_USART, "Manufacturer ID: 0x%X\r\n", sd_cardinfo.card_cid.mid);
        my_printf(DEBUG_USART, "OEM/Application ID: 0x%X\r\n", sd_cardinfo.card_cid.oid);

        /* Print product name (PNM). */
        uint8_t pnm[6];
        pnm[0] = (sd_cardinfo.card_cid.pnm0 >> 24) & 0xFF;
        pnm[1] = (sd_cardinfo.card_cid.pnm0 >> 16) & 0xFF;
        pnm[2] = (sd_cardinfo.card_cid.pnm0 >> 8) & 0xFF;
        pnm[3] = sd_cardinfo.card_cid.pnm0 & 0xFF;
        pnm[4] = sd_cardinfo.card_cid.pnm1 & 0xFF;
        pnm[5] = '\0';
        my_printf(DEBUG_USART, "Product Name: %s\r\n", pnm);

        /* Print product revision and serial number. */
        my_printf(DEBUG_USART, "Product Revision: %d.%d\r\n", (sd_cardinfo.card_cid.prv >> 4) & 0x0F, sd_cardinfo.card_cid.prv & 0x0F);
        my_printf(DEBUG_USART, "Product Serial Number: 0x%08X\r\n", sd_cardinfo.card_cid.psn);

        /* Print CSD structure version. */
        my_printf(DEBUG_USART, "CSD Version: %d.0\r\n", sd_cardinfo.card_csd.csd_struct + 1);

    }
    else
    {
        my_printf(DEBUG_USART, "\r\nFailed to get SD card information, error code: %d\r\n", status);
    }
}

static void sd_fatfs_long_name_test(void)
{
    static const char long_path[] =
        "0:/project_log_2026_04_22_device_gd32f470vet6_board_a_channel_01_run_0001_data_record_long_name_demo.txt";
    static const char long_msg[] = "FATFS_LONG_NAME_OK";
    FIL long_file;
    FRESULT fres;
    UINT io_len;
    uint8_t long_readback[64];
    uint32_t expect_len;

    expect_len = (uint32_t)strlen(long_msg);
    memset(long_readback, 0, sizeof(long_readback));

    fres = f_open(&long_file, long_path, FA_CREATE_ALWAYS | FA_WRITE);
    if (FR_OK != fres) {
        if (FR_INVALID_NAME == fres) {
            my_printf(DEBUG_USART,
                      "FATFS long-name open failed: FR_INVALID_NAME (enable LFN in ffconf.h)\r\n");
        } else {
            my_printf(DEBUG_USART, "FATFS long-name open(write) failed (%d)\r\n", fres);
        }
        return;
    }

    fres = f_write(&long_file, long_msg, (UINT)expect_len, &io_len);
    if ((FR_OK != fres) || (io_len != (UINT)expect_len)) {
        my_printf(DEBUG_USART,
                  "FATFS long-name write failed (res=%d, bw=%d)\r\n",
                  fres,
                  io_len);
        (void)f_close(&long_file);
        return;
    }

    fres = f_close(&long_file);
    if (FR_OK != fres) {
        my_printf(DEBUG_USART, "FATFS long-name close(write) failed (%d)\r\n", fres);
        return;
    }

    fres = f_open(&long_file, long_path, FA_OPEN_EXISTING | FA_READ);
    if (FR_OK != fres) {
        my_printf(DEBUG_USART, "FATFS long-name open(read) failed (%d)\r\n", fres);
        return;
    }

    io_len = 0;
    fres = f_read(&long_file, long_readback, sizeof(long_readback) - 1U, &io_len);
    (void)f_close(&long_file);
    if ((FR_OK != fres) || (io_len != (UINT)expect_len)) {
        my_printf(DEBUG_USART,
                  "FATFS long-name read failed (res=%d, br=%d)\r\n",
                  fres,
                  io_len);
        return;
    }

    if (0 == memcmp(long_readback, long_msg, expect_len)) {
        my_printf(DEBUG_USART, "FATFS long-name PASS: %s\r\n", long_path);
    } else {
        my_printf(DEBUG_USART, "FATFS long-name verify failed\r\n");
    }
}

void sd_fatfs_test(void)
{
    uint16_t k = 5;
    DSTATUS stat = 0;
    do
    {
        /* Initialize physical drive 0 (SD card). */
        stat = disk_initialize(0);
    }while((stat != 0) && (--k));

    card_info_get();

    my_printf(DEBUG_USART, "SD Card disk_initialize:%d\r\n",stat);

    /* Mount FAT file system on logical drive 0. */
    result = f_mount(0, &fs);
    my_printf(DEBUG_USART, "SD Card f_mount:%d\r\n", result);

    if ((RES_OK == stat) && (FR_OK == result))
    {
        my_printf(DEBUG_USART, "\r\nSD Card Initialize Success!\r\n");

        /* Create/overwrite and open file for write. */
        result = f_open(&fdst, "0:/FATFS.TXT", FA_CREATE_ALWAYS | FA_WRITE);

        sprintf((char *)filebuffer, "HELLO MCUSTUDIO");

        /* Write only valid text bytes, avoid trailing NUL padding in file. */
        result = f_write(&fdst, filebuffer, (UINT)strlen((char *)filebuffer), &bw);

        /* Write result check. */
        if(FR_OK == result)
            my_printf(DEBUG_USART, "FATFS FILE write Success!\r\n");
        else
        {
            my_printf(DEBUG_USART, "FATFS FILE write failed!\r\n");
        }

        /* Close file after write. */
        f_close(&fdst);

        /* Reopen existing file for read. */
        result = f_open(&fdst, "0:/FATFS.TXT", FA_OPEN_EXISTING | FA_READ);
        if (FR_OK != result)
        {
            my_printf(DEBUG_USART, "FATFS FILE open(read) failed! res=%d\r\n", result);
        }
        else
        {
            UINT expected_len = (UINT)strlen((char *)filebuffer);

            memset(buffer, 0, sizeof(buffer));
            br = 0;

            /* Read back file content. */
            result = f_read(&fdst, buffer, sizeof(buffer) - 1U, &br);

            if ((FR_OK == result) && (br == expected_len) &&
                (SUCCESS == memory_compare(buffer, filebuffer, (uint16_t)expected_len)))
            {
                buffer[br] = '\0';
                my_printf(DEBUG_USART, "FATFS Read File Success!\r\nThe content is:%s\r\n", buffer);
            }
            else
            {
                my_printf(DEBUG_USART,
                          "FATFS FILE read failed! res=%d br=%d expect=%d\r\n",
                          result,
                          br,
                          expected_len);
            }
        }

        /* Close file after read. */
        f_close(&fdst);

        /* Long filename create/readback check. */
        sd_fatfs_long_name_test();
    }
}

/* Compatibility wrappers for legacy call sites. */
void sd_lfs_init(void)
{
    sd_fatfs_init();
}

void sd_lfs_test(void)
{
    sd_fatfs_test();
}

/* Provide assert backend for libraries (e.g. littlefs) that require it. */
void __aeabi_assert(const char *expr, const char *file, int line)
{
    my_printf(DEBUG_USART,
              "ASSERT: %s, file: %s, line: %d\r\n",
              (NULL != expr) ? expr : "?",
              (NULL != file) ? file : "?",
              line);
    while (1) {
    }
}
