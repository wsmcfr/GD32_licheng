/*
this library is a 0.91'OLED(ssd1306) driver
*/


#include "oled.h"
#include "oledfont.h"

#define OLED_I2C_WAIT_TIMEOUT 100000U
#define OLED_I2C_ADDR_WRITE 0x78U
#define OLED_I2C_BUSY_WAIT_MS 10000U
#define OLED_CMD_CONTROL_BYTE 0x00U
#define OLED_DATA_CONTROL_BYTE 0x40U

uint8_t s_oled_available = 1U;

/*
 * 函数作用：
 *   对 OLED 所在 I2C0 总线执行手动恢复，并重新初始化 I2C 控制器。
 * 主要流程：
 *   1. 临时把 PB8/PB9 切为普通 GPIO 输出。
 *   2. 释放 SCL/SDA 后产生 9 个 SCL 脉冲，尝试让异常从机释放 SDA。
 *   3. 人工产生 STOP 条件后恢复 I2C0 复用开漏模式。
 *   4. 重新初始化 I2C0 时钟、地址和 ACK 配置。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   无返回值。
 */
static void I2C_Bus_Reset(void)
{
    uint8_t i;

    gpio_mode_set(OLED_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, OLED_CLK_PIN | OLED_DAT_PIN);
    gpio_output_options_set(OLED_PORT, GPIO_OTYPE_PP, GPIO_OSPEED_50MHZ, OLED_CLK_PIN | OLED_DAT_PIN);

    /* 先释放 SCL/SDA，给被打断的从机一个回到空闲态的窗口。 */
    gpio_bit_set(OLED_PORT, OLED_CLK_PIN | OLED_DAT_PIN);
    delay_ms(10);

    /* 9 个时钟覆盖从机可能残留的 8 位数据和 1 位 ACK 周期。 */
    for (i = 0U; i < 9U; i++) {
        gpio_bit_reset(OLED_PORT, OLED_CLK_PIN);
        delay_ms(5);
        gpio_bit_set(OLED_PORT, OLED_CLK_PIN);
        delay_ms(5);
    }

    /* SDA 在 SCL 高电平期间拉高就是 STOP，可把总线状态收回空闲。 */
    gpio_bit_set(OLED_PORT, OLED_CLK_PIN);
    gpio_bit_reset(OLED_PORT, OLED_DAT_PIN);
    delay_ms(5);
    gpio_bit_set(OLED_PORT, OLED_DAT_PIN);
    delay_ms(5);

    gpio_mode_set(OLED_PORT, GPIO_MODE_AF, GPIO_PUPD_PULLUP, OLED_DAT_PIN);
    gpio_output_options_set(OLED_PORT, GPIO_OTYPE_OD, GPIO_OSPEED_50MHZ, OLED_DAT_PIN);
    gpio_mode_set(OLED_PORT, GPIO_MODE_AF, GPIO_PUPD_PULLUP, OLED_CLK_PIN);
    gpio_output_options_set(OLED_PORT, GPIO_OTYPE_OD, GPIO_OSPEED_50MHZ, OLED_CLK_PIN);

    i2c_deinit(I2C0);
    i2c_clock_config(I2C0, 400000U, I2C_DTCY_2);
    i2c_mode_addr_config(I2C0, I2C_I2CMODE_ENABLE, I2C_ADDFORMAT_7BITS, I2C0_OWN_ADDRESS7);
    i2c_enable(I2C0);
    i2c_ack_config(I2C0, I2C_ACK_ENABLE);

    delay_ms(10);
}

/*
 * 函数作用：
 *   等待指定 I2C 标志位置位，用于给阻塞式传输增加超时保护。
 * 参数说明：
 *   i2c_periph：I2C 外设基地址，本驱动传入 I2C0。
 *   flag：需要等待的 I2C 标志位。
 *   timeout：轮询次数上限，避免硬件异常时永久卡死。
 * 返回值说明：
 *   1：目标标志在超时前置位。
 *   0：等待超时。
 */
static uint8_t oled_wait_i2c_flag_set(uint32_t i2c_periph, i2c_flag_enum flag, uint32_t timeout)
{
    while (timeout--) {
        if (SET == i2c_flag_get(i2c_periph, flag)) {
            return 1U;
        }
    }
    return 0U;
}

/*
 * 函数作用：
 *   等待 I2C STOP 位由硬件清零，确认 STOP 条件已经发送完成。
 * 参数说明：
 *   i2c_periph：I2C 外设基地址，本驱动传入 I2C0。
 *   timeout：轮询次数上限，避免总线异常时永久等待。
 * 返回值说明：
 *   1：STOP 位已清零。
 *   0：等待超时。
 */
static uint8_t oled_wait_i2c_stop_clear(uint32_t i2c_periph, uint32_t timeout)
{
    while (timeout--) {
        if (0U == (I2C_CTL0(i2c_periph) & I2C_CTL0_STOP)) {
            return 1U;
        }
    }
    return 0U;
}

/*
 * 函数作用：
 *   等待 DMA 通道传输完成标志，给 OLED I2C DMA 发送增加超时保护。
 * 参数说明：
 *   dma_periph：DMA 外设基地址，本驱动传入 DMA0。
 *   channel：DMA 通道号，本驱动使用 DMA_CH6。
 *   timeout：轮询次数上限。
 * 返回值说明：
 *   1：DMA 在超时前完成传输。
 *   0：等待超时。
 */
static uint8_t oled_wait_dma_ftf(uint32_t dma_periph, dma_channel_enum channel, uint32_t timeout)
{
    while (timeout--) {
        if (SET == dma_flag_get(dma_periph, channel, DMA_FLAG_FTF)) {
            return 1U;
        }
    }
    return 0U;
}

/*
 * 函数作用：
 *   等待地址发送完成，若 OLED 返回 NACK 则提前失败并清除 AERR。
 * 参数说明：
 *   timeout：轮询次数上限。
 * 返回值说明：
 *   1：OLED 地址阶段 ACK 成功。
 *   0：地址阶段超时或收到 NACK。
 */
static uint8_t oled_wait_addsend_or_nack(uint32_t timeout)
{
    while (timeout--) {
        if (SET == i2c_flag_get(I2C0, I2C_FLAG_ADDSEND)) {
            return 1U;
        }
        if (SET == i2c_flag_get(I2C0, I2C_FLAG_AERR)) {
            i2c_flag_clear(I2C0, I2C_FLAG_AERR);
            return 0U;
        }
    }
    return 0U;
}

/*
 * 函数作用：
 *   等待 I2C0 总线空闲；若总线忙则先尝试恢复，再做一次有限等待。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   1：总线已经进入空闲态，可以开始新的主机传输。
 *   0：恢复和等待后总线仍忙，本次 OLED 传输应放弃。
 */
static uint8_t oled_wait_bus_idle(void)
{
    uint32_t timeout = OLED_I2C_BUSY_WAIT_MS;

    if(i2c_flag_get(I2C0, I2C_FLAG_I2CBSY)) {
        I2C_Bus_Reset();
    }

    while(i2c_flag_get(I2C0, I2C_FLAG_I2CBSY) && (timeout > 0U)) {
        delay_ms(1);
        timeout--;
    }

    if(0U == timeout) {
        I2C_Bus_Reset();
        timeout = OLED_I2C_BUSY_WAIT_MS;
        while(i2c_flag_get(I2C0, I2C_FLAG_I2CBSY) && (timeout > 0U)) {
            delay_ms(1);
            timeout--;
        }
        if(0U == timeout) {
            return 0U;
        }
    }

    return 1U;
}

/*
 * 函数作用：
 *   通过一次 I2C START/地址/STOP 事务，把已经组包好的 OLED 数据交给 DMA 发送。
 * 参数说明：
 *   packet：待发送缓冲区指针，第 1 字节必须是 SSD1306 控制字。
 *   length：发送总字节数，包含控制字；最小 2 字节，最大受调用方缓冲限制。
 * 返回值说明：
 *   1：本次 I2C/DMA 传输完成。
 *   0：总线、地址、DMA 或 STOP 等阶段失败；函数会关闭 DMA 并尝试恢复总线。
 */
static uint8_t oled_write_packet(__IO uint8_t *packet, uint16_t length)
{
    if((NULL == packet) || (length < 2U)) {
        return 0U;
    }

    if(0U == oled_wait_bus_idle()) {
        return 0U;
    }

    i2c_start_on_bus(I2C0);
    if(!oled_wait_i2c_flag_set(I2C0, I2C_FLAG_SBSEND, OLED_I2C_WAIT_TIMEOUT)) {
        I2C_Bus_Reset();
        return 0U;
    }

    i2c_master_addressing(I2C0, OLED_I2C_ADDR_WRITE, I2C_TRANSMITTER);
    if(!oled_wait_addsend_or_nack(OLED_I2C_WAIT_TIMEOUT)) {
        i2c_stop_on_bus(I2C0);
        (void)oled_wait_i2c_stop_clear(I2C0, OLED_I2C_WAIT_TIMEOUT / 10U);
        I2C_Bus_Reset();
        return 0U;
    }

    i2c_flag_clear(I2C0, I2C_FLAG_ADDSEND);
    if(!oled_wait_i2c_flag_set(I2C0, I2C_FLAG_TBE, OLED_I2C_WAIT_TIMEOUT)) {
        I2C_Bus_Reset();
        return 0U;
    }

    dma_channel_disable(DMA0, DMA_CH6);
    dma_flag_clear(DMA0, DMA_CH6, DMA_FLAG_FEE | DMA_FLAG_SDE | DMA_FLAG_TAE | DMA_FLAG_HTF | DMA_FLAG_FTF);
    dma_memory_address_config(DMA0, DMA_CH6, DMA_MEMORY_0, (uint32_t)packet);
    dma_transfer_number_config(DMA0, DMA_CH6, length);
    i2c_dma_config(I2C0, I2C_DMA_ON);
    dma_channel_enable(DMA0, DMA_CH6);

    if(!oled_wait_dma_ftf(DMA0, DMA_CH6, OLED_I2C_WAIT_TIMEOUT)) {
        dma_channel_disable(DMA0, DMA_CH6);
        i2c_dma_config(I2C0, I2C_DMA_OFF);
        I2C_Bus_Reset();
        return 0U;
    }

    dma_flag_clear(DMA0, DMA_CH6, DMA_FLAG_FTF);
    dma_channel_disable(DMA0, DMA_CH6);
    i2c_dma_config(I2C0, I2C_DMA_OFF);

    /*
     * DMA 完成只表示字节已经搬入 I2C 数据寄存器/移位链路，STOP 前再等 BTC，
     * 避免最后一个字节尚未真正移出时就终止总线。
     */
    if(!oled_wait_i2c_flag_set(I2C0, I2C_FLAG_BTC, OLED_I2C_WAIT_TIMEOUT)) {
        I2C_Bus_Reset();
        return 0U;
    }

    i2c_stop_on_bus(I2C0);
    if(!oled_wait_i2c_stop_clear(I2C0, OLED_I2C_WAIT_TIMEOUT)) {
        I2C_Bus_Reset();
        return 0U;
    }

    return 1U;
}

/*
 * 函数作用：
 *   执行 OLED 组包发送，并在失败时关闭后续 OLED 传输，避免周期任务反复卡总线。
 * 参数说明：
 *   packet：待发送缓冲区指针，第 1 字节为控制字。
 *   length：发送总字节数，包含控制字。
 * 返回值说明：
 *   1：发送成功。
 *   0：发送失败，`s_oled_available` 会被置 0，直到 `OLED_Init()` 重新启用。
 */
static uint8_t oled_write_packet_checked(__IO uint8_t *packet, uint16_t length)
{
    if(0U == s_oled_available) {
        return 0U;
    }

    if(0U == oled_write_packet(packet, length)) {
        s_oled_available = 0U;
        return 0U;
    }

    return 1U;
}

static const uint8_t initcmd1[] = {
    0xAE,        /* 关闭显示，避免初始化中间状态闪屏。 */
    0xD5, 0x80,  /* 设置显示时钟分频和振荡频率。 */
    0xA8, 0x1F,  /* 128x32 屏使用 1/32 multiplex。 */
    0xD3, 0x00,  /* 显示偏移从 0 开始。 */
    0x40,        /* 设置显示起始行为 0。 */
    0x8D, 0x14,  /* 打开电荷泵，保证无外部升压时正常点亮。 */
    0xA1,        /* 段重映射，匹配当前屏幕排线方向。 */
    0xC8,        /* COM 扫描方向重映射。 */
    0xDA, 0x00,  /* 128x32 OLED 的 COM 引脚配置。 */
    0x81, 0x80,  /* 对比度默认值。 */
    0xD9, 0x1F,  /* 预充电周期。 */
    0xDB, 0x40,  /* VCOMH 取消选择电平。 */
    0xA4,        /* 使用 RAM 内容显示，不强制全亮。 */
    0xAF,        /* 初始化完成后打开显示。 */
};

/*
 * 函数作用：
 *   向 OLED 控制器发送 1 字节命令。
 * 参数说明：
 *   cmd：需要写入 SSD1306 的命令字节。
 * 返回值说明：
 *   无返回值。
 */
void OLED_Write_cmd(uint8_t cmd)
{
    OLED_Write_cmd_buf(&cmd, 1U);
}

/*
 * 函数作用：
 *   向 OLED 控制器连续发送多字节命令，内部按 DMA 数据缓冲区容量自动分块。
 * 主要流程：
 *   1. 把 SSD1306 命令控制字 0x00 放在 DMA 缓冲区首字节。
 *   2. 每次最多复制 128 字节命令到静态 DMA 缓冲区。
 *   3. 通过一次 I2C 事务发送“控制字 + 命令块”，减少连续命令的 START/STOP 开销。
 * 参数说明：
 *   cmds：待写入 SSD1306 的命令数组指针。
 *   length：命令字节数，不包含控制字；为 0 时直接返回。
 * 返回值说明：
 *   无返回值。
 */
void OLED_Write_cmd_buf(const uint8_t *cmds, uint16_t length)
{
    uint16_t offset = 0U;
    uint16_t chunk_len;
    uint16_t i;

    if((NULL == cmds) || (0U == length) || (0U == s_oled_available)) {
        return;
    }

    while(offset < length) {
        chunk_len = (uint16_t)(length - offset);
        if(chunk_len > OLED_TX_DATA_MAX_SIZE) {
            chunk_len = OLED_TX_DATA_MAX_SIZE;
        }

        /*
         * 复用数据 DMA 缓冲区发送命令块。这里函数是阻塞式，DMA 完成并 STOP 后才返回，
         * 因此下一次数据写入前覆盖缓冲区不会破坏正在进行的传输。
         */
        oled_data_buf[0] = OLED_CMD_CONTROL_BYTE;
        for(i = 0U; i < chunk_len; i++) {
            oled_data_buf[i + 1U] = cmds[offset + i];
        }

        if(0U == oled_write_packet_checked(oled_data_buf, (uint16_t)(chunk_len + 1U))) {
            return;
        }

        offset = (uint16_t)(offset + chunk_len);
    }
}

/*
 * 函数作用：
 *   向 OLED 显存写入 1 字节显示数据，保留给旧接口和零散写入场景使用。
 * 参数说明：
 *   data：需要写入当前 GDDRAM 地址的数据字节。
 * 返回值说明：
 *   无返回值。
 */
void OLED_Write_data(uint8_t data)
{
    OLED_Write_data_buf(&data, 1U);
}

/*
 * 函数作用：
 *   向 OLED 连续写入一段显存数据，内部按 DMA 缓冲区容量自动分块。
 * 主要流程：
 *   1. 把 SSD1306 数据控制字 0x40 放在 DMA 缓冲区首字节。
 *   2. 每次最多复制 128 字节显存数据到静态 DMA 缓冲区。
 *   3. 通过一次 I2C 事务发送“控制字 + 数据块”，减少 per-byte START/STOP 开销。
 * 参数说明：
 *   data：待写入的显存数据指针，调用者需保证数据在本函数返回前有效。
 *   length：显存数据字节数，不包含控制字；为 0 时直接返回。
 * 返回值说明：
 *   无返回值。
 */
void OLED_Write_data_buf(const uint8_t *data, uint16_t length)
{
    uint16_t offset = 0U;
    uint16_t chunk_len;
    uint16_t i;

    if((NULL == data) || (0U == length) || (0U == s_oled_available)) {
        return;
    }

    while(offset < length) {
        chunk_len = (uint16_t)(length - offset);
        if(chunk_len > OLED_TX_DATA_MAX_SIZE) {
            chunk_len = OLED_TX_DATA_MAX_SIZE;
        }

        oled_data_buf[0] = OLED_DATA_CONTROL_BYTE;
        for(i = 0U; i < chunk_len; i++) {
            oled_data_buf[i + 1U] = data[offset + i];
        }

        if(0U == oled_write_packet_checked(oled_data_buf, (uint16_t)(chunk_len + 1U))) {
            return;
        }

        offset = (uint16_t)(offset + chunk_len);
    }
}

/*
 * 函数作用：
 *   使用一次批量命令事务设置 OLED 页地址和列地址。
 * 参数说明：
 *   x：横向像素坐标，范围建议为 0~127。
 *   y：页坐标，128x32 屏范围建议为 0~3。
 * 返回值说明：
 *   无返回值。
 */
static void oled_set_position_buf(uint8_t x, uint8_t y)
{
    uint8_t pos_cmds[3];

    pos_cmds[0] = (uint8_t)(0xB0U + y);
    pos_cmds[1] = (uint8_t)(((x & 0xF0U) >> 4U) | 0x10U);
    pos_cmds[2] = (uint8_t)(x & 0x0FU);
    OLED_Write_cmd_buf(pos_cmds, sizeof(pos_cmds));
}

/*
 * 函数作用：
 *   在指定页范围内显示一幅位图。
 * 参数说明：
 *   x0：位图左上角横向起始坐标。
 *   y0：位图左上角页坐标。
 *   x1：位图横向结束坐标，发送范围不包含该坐标。
 *   y1：位图页结束坐标，发送范围不包含该页。
 *   BMP：位图数据数组指针，按页连续排列。
 * 返回值说明：
 *   无返回值。
 */
void OLED_ShowPic(uint8_t x0, uint8_t y0, uint8_t x1, uint8_t y1, uint8_t BMP[])
{
    uint16_t i = 0;
    uint8_t y;
    uint8_t width;

    if((NULL == BMP) || (x1 <= x0) || (x0 >= OLED_WIDTH) || (y0 >= (OLED_HEIGHT / 8U))) {
        return;
    }

    if(x1 > OLED_WIDTH) {
        x1 = OLED_WIDTH;
    }
    if(y1 > (OLED_HEIGHT / 8U)) {
        y1 = (OLED_HEIGHT / 8U);
    }

    width = (uint8_t)(x1 - x0);
    for (y = y0; y < y1; y++)
    {
        OLED_Set_Position(x0, y);
        OLED_Write_data_buf(&BMP[i], width);
        i = (uint16_t)(i + width);
    }
}

/*
 * 函数作用：
 *   在 OLED 指定位置显示一个 16x16 汉字字模。
 * 参数说明：
 *   x：显示起始横坐标。
 *   y：显示起始页坐标。
 *   no：汉字字库序号。
 * 返回值说明：
 *   无返回值。
 */
void OLED_ShowHanzi(uint8_t x, uint8_t y, uint8_t no)
{
    OLED_Set_Position(x, y);
    OLED_Write_data_buf(Hzk[2U * no], 16U);
    OLED_Set_Position(x, y + 1);
    OLED_Write_data_buf(Hzk[(2U * no) + 1U], 16U);
}

/*
 * 函数作用：
 *   在 OLED 指定位置显示一个 32x32 大号汉字字模。
 * 参数说明：
 *   x：显示起始横坐标。
 *   y：显示起始页坐标。
 *   n：大号汉字字库序号。
 * 返回值说明：
 *   无返回值。
 */
void OLED_ShowHzbig(uint8_t x, uint8_t y, uint8_t n)
{
    OLED_Set_Position(x, y);
    OLED_Write_data_buf(Hzb[4U * n], 32U);
    OLED_Set_Position(x, y + 1);
    OLED_Write_data_buf(Hzb[(4U * n) + 1U], 32U);

    OLED_Set_Position(x, y + 2);
    OLED_Write_data_buf(Hzb[(4U * n) + 2U], 32U);
    OLED_Set_Position(x, y + 3);
    OLED_Write_data_buf(Hzb[(4U * n) + 3U], 32U);
}

/**
 * @note	
*/
void OLED_ShowFloat(uint8_t x, uint8_t y, float num, uint8_t accuracy, uint8_t fontsize)
{
    uint8_t i = 0;
    uint8_t j = 0;
    uint8_t t = 0;
    uint8_t temp = 0;
    uint16_t numel = 0;
    uint32_t integer = 0;
    float decimals = 0;

    if (num < 0)
    {
        OLED_ShowChar(x, y, '-', fontsize);
        num = 0 - num;
        i++;
    }

    integer = (uint32_t)num;
    decimals = num - integer;

    if (integer)
    {
        numel = integer;

        while (numel)
        {
            numel /= 10;
            j++;
        }
        i += (j - 1);
        for (temp = 0; temp < j; temp++)
        {
            OLED_ShowChar(x + 8 * (i - temp), y, integer % 10 + '0', fontsize);
            integer /= 10;
        }
    }
    else
    {
        OLED_ShowChar(x + 8 * i, y, temp + '0', fontsize);
    }
    i++;
    if (accuracy)
    {
        OLED_ShowChar(x + 8 * i, y, '.', fontsize);

        i++;
        for (t = 0; t < accuracy; t++)
        {
            decimals *= 10;
            temp = (uint8_t)decimals;
            OLED_ShowChar(x + 8 * (i + t), y, temp + '0', fontsize);
            decimals -= temp;
        }
    }
}

/**
 * @param m - base
 * @param n - exponent
 * @return result
*/
static uint32_t OLED_Pow(uint8_t a, uint8_t n)
{
    uint32_t result = 1;
    while (n--)
    {
        result *= a;
    }
    return result;
}

/**
 * @note	
*/
void OLED_ShowNum(uint8_t x, uint8_t y, uint32_t num, uint8_t length, uint8_t fontsize)
{
    uint8_t t, temp;
    uint8_t enshow = 0;
    for (t = 0; t < length; t++)
    {
        temp = (num / OLED_Pow(10, length - t - 1)) % 10;
        if (enshow == 0 && t < (length - 1))
        {
            if (temp == 0)
            {
                OLED_ShowChar(x + (fontsize / 2) * t, y, ' ', fontsize);
                continue;
            }
            else
                enshow = 1;
        }
        OLED_ShowChar(x + (fontsize / 2) * t, y, temp + '0', fontsize);
    }
}

/*
 * 函数作用：
 *   按 6x8 字库把一段 ASCII 字符串拼成连续显存数据后批量写入。
 * 参数说明：
 *   x：显示起始横坐标。
 *   y：显示起始页坐标。
 *   ch：待显示字符串指针，需以 '\0' 结束。
 * 返回值说明：
 *   无返回值。
 */
static void oled_show_str_8x6(uint8_t x, uint8_t y, const char *ch)
{
    uint8_t row_buf[OLED_TX_DATA_MAX_SIZE];
    uint8_t row_len = 0U;
    uint8_t current_x = x;
    uint8_t c;
    uint8_t i;

    /*
     * 6x8 字符实际字模宽度为 6 列，但旧接口按 8 像素步进排版。
     * 这里每个字符补 2 列空白，保证批量写入后的坐标、间距和差异段刷新
     * 都与原来的 x += 8 行为一致。
     */
    while((*ch != '\0') && (y < (OLED_HEIGHT / 8U))) {
        row_len = 0U;
        current_x = x;
        while((*ch != '\0') && (current_x <= (OLED_WIDTH - 8U)) && ((uint16_t)row_len + 8U <= OLED_TX_DATA_MAX_SIZE)) {
            c = (uint8_t)(*ch);
            if((c < ' ') || (c > '~')) {
                c = ' ';
            }
            c = (uint8_t)(c - ' ');

            for(i = 0U; i < 6U; i++) {
                row_buf[row_len + i] = F6X8[c][i];
            }

            row_buf[row_len + 6U] = 0x00U;
            row_buf[row_len + 7U] = 0x00U;
            row_len = (uint8_t)(row_len + 8U);
            current_x = (uint8_t)(current_x + 8U);
            ch++;
        }

        if(row_len > 0U) {
            OLED_Set_Position(x, y);
            OLED_Write_data_buf(row_buf, row_len);
        }

        if(*ch != '\0') {
            x = 0U;
            y = (uint8_t)(y + 2U);
        }
    }
}

/*
 * 函数作用：
 *   按 8x16 字库把一段 ASCII 字符串的上下两页分别拼成连续显存数据后批量写入。
 * 参数说明：
 *   x：显示起始横坐标。
 *   y：显示起始页坐标。
 *   ch：待显示字符串指针，需以 '\0' 结束。
 * 返回值说明：
 *   无返回值。
 */
static void oled_show_str_16x8(uint8_t x, uint8_t y, const char *ch)
{
    uint8_t upper_buf[OLED_TX_DATA_MAX_SIZE];
    uint8_t lower_buf[OLED_TX_DATA_MAX_SIZE];
    uint8_t row_len = 0U;
    uint8_t current_x = x;
    uint8_t c;
    uint8_t i;

    while((*ch != '\0') && ((uint8_t)(y + 1U) < (OLED_HEIGHT / 8U))) {
        row_len = 0U;
        current_x = x;
        while((*ch != '\0') && (current_x <= (OLED_WIDTH - 8U)) && ((uint16_t)row_len + 8U <= OLED_TX_DATA_MAX_SIZE)) {
            c = (uint8_t)(*ch);
            if((c < ' ') || (c > '~')) {
                c = ' ';
            }
            c = (uint8_t)(c - ' ');

            for(i = 0U; i < 8U; i++) {
                upper_buf[row_len + i] = F8X16[(c * 16U) + i];
                lower_buf[row_len + i] = F8X16[(c * 16U) + 8U + i];
            }

            row_len = (uint8_t)(row_len + 8U);
            current_x = (uint8_t)(current_x + 8U);
            ch++;
        }

        if(row_len > 0U) {
            OLED_Set_Position(x, y);
            OLED_Write_data_buf(upper_buf, row_len);
            OLED_Set_Position(x, (uint8_t)(y + 1U));
            OLED_Write_data_buf(lower_buf, row_len);
        }

        if(*ch != '\0') {
            x = 0U;
            y = (uint8_t)(y + 2U);
        }
    }
}


/*
 * 函数作用：
 *   在 OLED 指定位置显示字符串。
 * 参数说明：
 *   x：显示起始横坐标。
 *   y：显示起始页坐标。
 *   ch：待显示字符串指针，需以 '\0' 结束。
 *   fontsize：字体高度，支持 8 或 16。
 * 返回值说明：
 *   无返回值。
 */
void OLED_ShowStr(uint8_t x, uint8_t y, char *ch, uint8_t fontsize)
{
    if(NULL == ch) {
        return;
    }

    if(fontsize == 16U) {
        oled_show_str_16x8(x, y, ch);
    } else {
        oled_show_str_8x6(x, y, ch);
    }
}

/*
 * 函数作用：
 *   在 OLED 指定位置显示单个 ASCII 字符。
 * 参数说明：
 *   x：显示起始横坐标。
 *   y：显示起始页坐标。
 *   ch：待显示字符的 ASCII 编码；超出 0x20~0x7E 时按空格处理。
 *   fontsize：字体高度，16 使用 8x16 字库，其余值使用 6x8 字库。
 * 返回值说明：
 *   无返回值。
 */
void OLED_ShowChar(uint8_t x, uint8_t y, uint8_t ch, uint8_t fontsize)
{
    uint8_t c;

    /*
     * 字库只覆盖标准可打印 ASCII。越界字符按空格显示，避免负偏移或过大
     * 下标读穿字库数组。
     */
    if((ch < ' ') || (ch > '~')) {
        ch = ' ';
    }
    c = (uint8_t)(ch - ' ');

    if (x > 127U)
    {
        x = 0;
        y++;
    }

    if (fontsize == 16)
    {
        OLED_Set_Position(x, y);
        OLED_Write_data_buf(&F8X16[c * 16U], 8U);
        OLED_Set_Position(x, y + 1);
        OLED_Write_data_buf(&F8X16[(c * 16U) + 8U], 8U);
    }
    else
    {
        OLED_Set_Position(x, y);
        OLED_Write_data_buf(F6X8[c], 6U);
    }
}


/*
 * 函数作用：
 *   将 OLED 全屏填充为亮点状态。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   无返回值。
 */
void OLED_Allfill(void)
{
    uint8_t fill[OLED_TX_DATA_MAX_SIZE];
    uint8_t pos_cmds[3];
    uint8_t i;

    /*
     * 全亮填充值只在手动测试或特殊显示场景使用，放在栈上临时生成即可，
     * 避免为了少量低频调用长期占用一份全 0xFF 的全局常量空间。
     */
    memset(fill, 0xFF, sizeof(fill));

    for (i = 0; i < 4; i++)
    {
        pos_cmds[0] = (uint8_t)(0xB0U + i);
        pos_cmds[1] = 0x10U;
        pos_cmds[2] = 0x00U;
        OLED_Write_cmd_buf(pos_cmds, sizeof(pos_cmds));
        OLED_Write_data_buf(fill, OLED_TX_DATA_MAX_SIZE);
    }
}

/*
 * 函数作用：
 *   设置 OLED 后续写入的页地址和列地址。
 * 参数说明：
 *   x：横向像素坐标，范围建议为 0~127。
 *   y：页坐标，128x32 屏范围建议为 0~3。
 * 返回值说明：
 *   无返回值。
 */
void OLED_Set_Position(uint8_t x, uint8_t y)
{
    oled_set_position_buf(x, y);
}

/*
 * 函数作用：
 *   清空 OLED 全屏显存。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   无返回值。
 */
void OLED_Clear(void)
{
    static const uint8_t zeros[OLED_TX_DATA_MAX_SIZE] = {0U};
    uint8_t pos_cmds[3];
    uint8_t i;

    for (i = 0; i < 4; i++)
    {
        pos_cmds[0] = (uint8_t)(0xB0U + i);
        pos_cmds[1] = 0x10U;
        pos_cmds[2] = 0x00U;
        OLED_Write_cmd_buf(pos_cmds, sizeof(pos_cmds));
        OLED_Write_data_buf(zeros, OLED_TX_DATA_MAX_SIZE);
    }
}

/*
 * 函数作用：
 *   打开 OLED 电荷泵和显示输出。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   无返回值。
 */
void OLED_Display_On(void)
{
    static const uint8_t display_on_cmds[] = {0x8DU, 0x14U, 0xAFU};

    OLED_Write_cmd_buf(display_on_cmds, sizeof(display_on_cmds));
}

/*
 * 函数作用：
 *   关闭 OLED 电荷泵和显示输出，用于息屏或进入低功耗前收拢显示器状态。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   无返回值。
 */
void OLED_Display_Off(void)
{
    static const uint8_t display_off_cmds[] = {0x8DU, 0x10U, 0xAEU};

    /*
     * 先关闭电荷泵，再发送 Display OFF 命令。
     * 这里必须使用 0xAE 关屏；若误发 0xAF，只会保持显示开启，
     * 深度睡眠前的 OLED 息屏和降功耗就都不会真正生效。
     */
    OLED_Write_cmd_buf(display_off_cmds, sizeof(display_off_cmds));
}

/*
 * 函数作用：
 *   初始化 SSD1306 OLED 控制器，并清屏后把写入位置复位到左上角。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   无返回值。
 */
void OLED_Init(void)
{
    delay_ms(100);
    s_oled_available = 1U;

    OLED_Write_cmd_buf(initcmd1, sizeof(initcmd1));
    if (0U == s_oled_available) {
        return;
    }

    OLED_Clear();
    OLED_Set_Position(0, 0);
}











