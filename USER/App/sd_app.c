#include "sd_app.h"

FATFS fs;
FIL fdst;
uint16_t i = 0, count, result = 0;
UINT br, bw;

sd_card_info_struct sd_cardinfo;

BYTE buffer[128];
BYTE filebuffer[128];

/*
 * 函数作用：
 *   比较两段缓存区内容是否完全一致。
 * 主要流程：
 *   1. 按 length 指定的字节数逐字节比较 src 和 dst。
 *   2. 任意字节不同则立即返回 ERROR。
 *   3. 全部字节相同后返回 SUCCESS。
 * 参数说明：
 *   src：第一段待比较缓冲区起始地址，调用者需保证至少包含 length 字节。
 *   dst：第二段待比较缓冲区起始地址，调用者需保证至少包含 length 字节。
 *   length：需要比较的字节数，单位为字节。
 * 返回值说明：
 *   SUCCESS：表示全部字节相同。
 *   ERROR：表示至少存在一个字节不同。
 */
static ErrStatus memory_compare(uint8_t* src, uint8_t* dst, uint16_t length)
{
    while(length --){
        if(*src++ != *dst++)
            return ERROR;
    }
    return SUCCESS;
}

/*
 * 函数作用：
 *   打开 SDIO 中断，为后续 SD 卡物理层和 FatFs 访问准备中断入口。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   无返回值。
 */
void sd_fatfs_init(void)
{
    /* SDIO 传输依赖中断完成状态推进，因此在文件系统测试前先打开中断入口。 */
    nvic_irq_enable(SDIO_IRQn, 0, 0);
}

/*
 * 函数作用：
 *   读取 SD 卡 CID/CSD 信息，并通过调试串口打印卡类型、容量和标识字段。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   无返回值。
 */
void card_info_get(void)
{
    sd_card_info_struct sd_cardinfo;
    sd_error_enum status;
    uint32_t block_count, block_size;

    /* 先读取 CID/CSD，后续容量、类型和产品信息都来自该结构。 */
    status = sd_card_information_get(&sd_cardinfo);

    if(SD_OK == status)
    {
        my_printf(DEBUG_USART, "\r\n*** SD Card Info ***\r\n");

        /* 卡类型枚举来自 SDIO 组件，打印为可读文本便于串口日志诊断。 */
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

        /* 容量和块信息用于确认 SD 卡识别结果是否符合实际介质。 */
        block_count = (sd_cardinfo.card_csd.c_size + 1) * 1024;
        block_size = 512;
        my_printf(DEBUG_USART,"\r\n## Device size is %dKB (%.2fGB)##", sd_card_capacity_get(), sd_card_capacity_get() / 1024.0f / 1024.0f);
        my_printf(DEBUG_USART,"\r\n## Block size is %dB ##", block_size);
        my_printf(DEBUG_USART,"\r\n## Block count is %d ##", block_count);

        /* 厂商、OEM、产品名和序列号用于区分不同 SD 卡样品。 */
        my_printf(DEBUG_USART, "Manufacturer ID: 0x%X\r\n", sd_cardinfo.card_cid.mid);
        my_printf(DEBUG_USART, "OEM/Application ID: 0x%X\r\n", sd_cardinfo.card_cid.oid);

        uint8_t pnm[6];
        pnm[0] = (sd_cardinfo.card_cid.pnm0 >> 24) & 0xFF;
        pnm[1] = (sd_cardinfo.card_cid.pnm0 >> 16) & 0xFF;
        pnm[2] = (sd_cardinfo.card_cid.pnm0 >> 8) & 0xFF;
        pnm[3] = sd_cardinfo.card_cid.pnm0 & 0xFF;
        pnm[4] = sd_cardinfo.card_cid.pnm1 & 0xFF;
        pnm[5] = '\0';
        my_printf(DEBUG_USART, "Product Name: %s\r\n", pnm);

        my_printf(DEBUG_USART, "Product Revision: %d.%d\r\n", (sd_cardinfo.card_cid.prv >> 4) & 0x0F, sd_cardinfo.card_cid.prv & 0x0F);
        my_printf(DEBUG_USART, "Product Serial Number: 0x%08X\r\n", sd_cardinfo.card_cid.psn);

        /* CSD 版本能帮助判断容量字段解释方式是否与卡类型匹配。 */
        my_printf(DEBUG_USART, "CSD Version: %d.0\r\n", sd_cardinfo.card_csd.csd_struct + 1);

    }
    else
    {
        my_printf(DEBUG_USART, "\r\nFailed to get SD card information, error code: %d\r\n", status);
    }
}

/*
 * 函数作用：
 *   验证当前 FatFs 配置是否支持长文件名创建、写入、读取和内容校验。
 * 主要流程：
 *   1. 使用较长路径创建测试文件并写入固定字符串。
 *   2. 关闭后重新打开读取，确认读取长度与预期一致。
 *   3. 使用 memcmp 校验读回内容并打印 PASS/FAIL。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   无返回值；失败原因通过调试串口输出。
 */
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
        /* FR_INVALID_NAME 通常表示 FatFs 长文件名配置未开启，单独提示便于定位 ffconf.h。 */
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
        /* 写入长度必须与期望长度完全一致，否则后续读回校验没有意义。 */
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
        /* 长文件名测试只接受完整读回，避免把部分读成功误判为通过。 */
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

/*
 * 函数作用：
 *   执行 SD 卡 FatFs 冒烟测试，覆盖初始化、挂载、短文件名读写和长文件名读写。
 * 主要流程：
 *   1. 重试初始化物理磁盘，降低上电初期 SD 卡未就绪带来的偶发失败。
 *   2. 挂载 FatFs 文件系统。
 *   3. 创建 FATFS.TXT，写入固定文本并读回校验。
 *   4. 额外运行长文件名测试，确认 ffconf.h 的 LFN 配置有效。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   无返回值；测试过程和失败原因通过调试串口输出。
 */
void sd_fatfs_test(void)
{
    uint16_t k = 5;
    DSTATUS stat = 0;
    do
    {
        /* SD 卡上电后可能还未完全就绪，这里保留有限重试而不是无限阻塞。 */
        stat = disk_initialize(0);
    }while((stat != 0) && (--k));

    card_info_get();

    my_printf(DEBUG_USART, "SD Card disk_initialize:%d\r\n",stat);

    /* 只有物理层初始化和文件系统挂载都成功，才继续做文件读写验证。 */
    result = f_mount(0, &fs);
    my_printf(DEBUG_USART, "SD Card f_mount:%d\r\n", result);

    if ((RES_OK == stat) && (FR_OK == result))
    {
        my_printf(DEBUG_USART, "\r\nSD Card Initialize Success!\r\n");

        /* 每次测试都覆盖写入同一个文件，保证读回内容来自本轮写入。 */
        result = f_open(&fdst, "0:/FATFS.TXT", FA_CREATE_ALWAYS | FA_WRITE);

        sprintf((char *)filebuffer, "HELLO MCUSTUDIO");

        /* 只写入有效字符串长度，避免把缓冲区尾部的 NUL 填充写进文件。 */
        result = f_write(&fdst, filebuffer, (UINT)strlen((char *)filebuffer), &bw);

        if(FR_OK == result)
            my_printf(DEBUG_USART, "FATFS FILE write Success!\r\n");
        else
        {
            my_printf(DEBUG_USART, "FATFS FILE write failed!\r\n");
        }

        /* 写完必须关闭文件，确保 FatFs 刷新目录项和缓存后再重新打开读取。 */
        f_close(&fdst);

        /* 重新以只读方式打开，验证真实存储介质上的内容，而不是复用写缓存。 */
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

            /* 读回时预留 1 字节给字符串结束符，便于后续按文本打印。 */
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

        /* 无论读回是否成功，都关闭文件句柄，避免影响下一次文件系统操作。 */
        f_close(&fdst);

        sd_fatfs_long_name_test();
    }
}

/*
 * 函数作用：
 *   兼容旧接口名称，将旧的 sd_lfs_init() 调用转发到新的 FatFs 初始化接口。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   无返回值。
 */
void sd_lfs_init(void)
{
    sd_fatfs_init();
}

/*
 * 函数作用：
 *   兼容旧接口名称，将旧的 sd_lfs_test() 调用转发到新的 FatFs 测试接口。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   无返回值。
 */
void sd_lfs_test(void)
{
    sd_fatfs_test();
}

/*
 * 函数作用：
 *   为需要 assert 后端的第三方库提供最小实现，打印断言上下文后停机。
 * 参数说明：
 *   expr：触发断言的表达式字符串，允许为空指针，日志中会替换为 "?"。
 *   file：触发断言的源文件名，允许为空指针，日志中会替换为 "?"。
 *   line：触发断言的源代码行号。
 * 返回值说明：
 *   无返回值；该函数进入无限循环，作为不可恢复错误的停机点。
 */
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
