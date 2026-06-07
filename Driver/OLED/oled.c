#include "oled.h"
#include "oledfont.h"

#define OLED_I2C_WAIT_TIMEOUT 100000
#define OLED_I2C_ADDR_WRITE 0x78U
#define OLED_I2C_BUSY_WAIT_MS 20
#define OLED_CMD_CONTROL_BYTE 0x00U
#define OLED_DATA_CONTROL_BYTE 0x40U
#define OLED_WIDTH  128
#define OLED_HEIGHT 32

static uint8_t s_oled_available = 1;

static void I2C_Bus_Reset(void)
{
    uint8_t i;

    gpio_mode_set(OLED_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, OLED_CLK_PIN | OLED_DAT_PIN);
    gpio_output_options_set(OLED_PORT, GPIO_OTYPE_PP, GPIO_OSPEED_50MHZ, OLED_CLK_PIN | OLED_DAT_PIN);


    gpio_bit_set(OLED_PORT, OLED_CLK_PIN | OLED_DAT_PIN);
    delay_ms(10);

    for (i = 0; i < 9; i++)
	{
        gpio_bit_reset(OLED_PORT, OLED_CLK_PIN);
        delay_ms(5);
        gpio_bit_set(OLED_PORT, OLED_CLK_PIN);
        delay_ms(5);
    }

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
    i2c_clock_config(I2C0, 400000, I2C_DTCY_2);
    i2c_mode_addr_config(I2C0, I2C_I2CMODE_ENABLE, I2C_ADDFORMAT_7BITS, I2C0_OWN_ADDRESS7);
    i2c_enable(I2C0);
    i2c_ack_config(I2C0, I2C_ACK_ENABLE);

    delay_ms(10);
}

// 等待指定 I2C 标志位置位，timeout 内未置位返回 0。
static uint8_t oled_wait_i2c_flag_set(uint32_t i2c_periph, i2c_flag_enum flag, uint32_t timeout)
{
    while (timeout--)
	{
        if (SET == i2c_flag_get(i2c_periph, flag))
		{
            return 1;
        }
    }
    return 0;
}

// 等待 I2C STOP 位由硬件清零
static uint8_t oled_wait_i2c_stop_clear(uint32_t i2c_periph, uint32_t timeout)
{
    while (timeout--)
	{
        if (0 == (I2C_CTL0(i2c_periph) & I2C_CTL0_STOP))
		{
            return 1;
        }
    }
    return 0;
}

// 等待 DMA 通道传输完成标志
static uint8_t oled_wait_dma_ftf(uint32_t dma_periph, dma_channel_enum channel, uint32_t timeout)
{
    while (timeout--)
	{
        if (SET == dma_flag_get(dma_periph, channel, DMA_FLAG_FTF))
		{
            return 1;
        }
    }
    return 0;
}

// 等待地址发送完成
static uint8_t oled_wait_addsend_or_nack(uint32_t timeout)
{
    while (timeout--)
	{
        if (SET == i2c_flag_get(I2C0, I2C_FLAG_ADDSEND))
		{
            return 1;
        }
        if (SET == i2c_flag_get(I2C0, I2C_FLAG_AERR))
		{
            i2c_flag_clear(I2C0, I2C_FLAG_AERR);
            return 0;
        }
    }
    return 0;
}

/*
 * 等待 I2C0 总线空闲
 */
static uint8_t oled_wait_bus_idle(void)
{
    uint32_t timeout = OLED_I2C_BUSY_WAIT_MS;

    if(i2c_flag_get(I2C0, I2C_FLAG_I2CBSY))
	{
        I2C_Bus_Reset();
    }

    while(i2c_flag_get(I2C0, I2C_FLAG_I2CBSY) && (timeout > 0))
	{
        delay_ms(1);
        timeout--;
    }

    if(0 == timeout)
	{
        I2C_Bus_Reset();
        timeout = OLED_I2C_BUSY_WAIT_MS;
        while(i2c_flag_get(I2C0, I2C_FLAG_I2CBSY) && (timeout > 0))
		{
            delay_ms(1);
            timeout--;
        }
        if(0 == timeout)
		{
            return 0;
        }
    }

    return 1;
}


// 通过一次 I2C START/地址/DMA/STOP 事务把已组包好的 OLED 数据发出去。
static uint8_t oled_write_packet(__IO uint8_t *packet, uint16_t length)
{
    if(!packet || (length < 2))
	{
        return 0;
    }

    if(!oled_wait_bus_idle())
	{
        return 0;
    }

    i2c_start_on_bus(I2C0);
    if(!oled_wait_i2c_flag_set(I2C0, I2C_FLAG_SBSEND, OLED_I2C_WAIT_TIMEOUT))
	{
        I2C_Bus_Reset();
        return 0;
    }

    i2c_master_addressing(I2C0, OLED_I2C_ADDR_WRITE, I2C_TRANSMITTER);
    if(!oled_wait_addsend_or_nack(OLED_I2C_WAIT_TIMEOUT))
	{
        i2c_stop_on_bus(I2C0);
        (void)oled_wait_i2c_stop_clear(I2C0, OLED_I2C_WAIT_TIMEOUT / 10);
        I2C_Bus_Reset();
        return 0;
    }

    i2c_flag_clear(I2C0, I2C_FLAG_ADDSEND);
    if(!oled_wait_i2c_flag_set(I2C0, I2C_FLAG_TBE, OLED_I2C_WAIT_TIMEOUT))
	{
        I2C_Bus_Reset();
        return 0;
    }

    dma_channel_disable(DMA0, DMA_CH6);
    dma_flag_clear(DMA0, DMA_CH6, DMA_FLAG_FEE | DMA_FLAG_SDE | DMA_FLAG_TAE | DMA_FLAG_HTF | DMA_FLAG_FTF);
    dma_memory_address_config(DMA0, DMA_CH6, DMA_MEMORY_0, (uint32_t)packet);
    dma_transfer_number_config(DMA0, DMA_CH6, length);
    i2c_dma_config(I2C0, I2C_DMA_ON);
    dma_channel_enable(DMA0, DMA_CH6);

    if(!oled_wait_dma_ftf(DMA0, DMA_CH6, OLED_I2C_WAIT_TIMEOUT))
	{
        dma_channel_disable(DMA0, DMA_CH6);
        i2c_dma_config(I2C0, I2C_DMA_OFF);
        I2C_Bus_Reset();
        return 0;
    }

    dma_flag_clear(DMA0, DMA_CH6, DMA_FLAG_FTF);
    dma_channel_disable(DMA0, DMA_CH6);
    i2c_dma_config(I2C0, I2C_DMA_OFF);

    if(!oled_wait_i2c_flag_set(I2C0, I2C_FLAG_BTC, OLED_I2C_WAIT_TIMEOUT))
	{
        I2C_Bus_Reset();
        return 0;
    }

    i2c_stop_on_bus(I2C0);
    if(!oled_wait_i2c_stop_clear(I2C0, OLED_I2C_WAIT_TIMEOUT))
	{
        I2C_Bus_Reset();
        return 0;
    }

    return 1;
}

// 带可用标志保护的发送
static uint8_t oled_write_packet_checked(__IO uint8_t *packet, uint16_t length)
{
    if(!s_oled_available)
	{
        return 0;
    }

    if(!oled_write_packet(packet, length))
	{
        s_oled_available = 0;
        return 0;
    }

    return 1;
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

/* 连续发送SSD1306命令，按DMA缓冲自动分块。 */
static uint8_t oled_write_cmd_buf(const uint8_t *cmds, uint16_t length)
{
    uint16_t offset = 0;
    uint16_t chunk_len;
    uint16_t i;

    if(0 == length)
	{
        return 1;
    }
    if(!cmds || !s_oled_available)
	{
        return 0;
    }

    while(offset < length)
	{
        chunk_len = (uint16_t)(length - offset);
        if(chunk_len > OLED_TX_DATA_MAX_SIZE)
		{
            chunk_len = OLED_TX_DATA_MAX_SIZE;
        }

        oled_data_buf[0] = OLED_CMD_CONTROL_BYTE;
        for(i = 0; i < chunk_len; i++)
		{
            oled_data_buf[i + 1] = cmds[offset + i];
        }

        if(!oled_write_packet_checked(oled_data_buf, (uint16_t)(chunk_len + 1)))
		{
            return 0;
        }

        offset = (uint16_t)(offset + chunk_len);
    }

    return 1;
}

/* 连续写OLED显存，按DMA缓冲自动分块。 */
static uint8_t oled_write_data_buf(const uint8_t *data, uint16_t length)
{
    uint16_t offset = 0;
    uint16_t chunk_len;
    uint16_t i;

    if(0 == length)
	{
        return 1;
    }
    if(!data || !s_oled_available)
	{
        return 0;
    }

    while(offset < length)
	{
        chunk_len = (uint16_t)(length - offset);
        if(chunk_len > OLED_TX_DATA_MAX_SIZE)
		{
            chunk_len = OLED_TX_DATA_MAX_SIZE;
        }

        oled_data_buf[0] = OLED_DATA_CONTROL_BYTE;
        for(i = 0; i < chunk_len; i++)
		{
            oled_data_buf[i + 1] = data[offset + i];
        }

        if(!oled_write_packet_checked(oled_data_buf, (uint16_t)(chunk_len + 1)))
		{
            return 0;
        }

        offset = (uint16_t)(offset + chunk_len);
    }

    return 1;
}

// 用一次批量命令事务设置 OLED 页地址和列地址
static uint8_t oled_set_position_buf(uint8_t x, uint8_t y)
{
    uint8_t pos_cmds[3];

    if((x >= OLED_WIDTH) || (y >= (OLED_HEIGHT / 8)))
	{
        return 0;
    }

    pos_cmds[0] = (uint8_t)(0xB0U + y);
    pos_cmds[1] = (uint8_t)(((x & 0xF0U) >> 4) | 0x10U);
    pos_cmds[2] = (uint8_t)(x & 0x0FU);
    return oled_write_cmd_buf(pos_cmds, sizeof(pos_cmds));
}

/* 按8x16字库写ASCII字符串，超宽自动换到下一逻辑行（y+=2）。 */
static uint8_t oled_show_ascii_16(uint8_t x, uint8_t y, const char *ch)
{
    uint8_t upper_buf[OLED_TX_DATA_MAX_SIZE];
    uint8_t lower_buf[OLED_TX_DATA_MAX_SIZE];
    uint8_t row_len = 0;
    uint8_t current_x = x;
    uint8_t c;
    uint8_t i;

    if(!ch || (x >= OLED_WIDTH) || ((uint8_t)(y + 1) >= (OLED_HEIGHT / 8)))
	{
        return 0;
    }

    while((*ch != '\0') && ((uint8_t)(y + 1) < (OLED_HEIGHT / 8)))
	{
        row_len = 0;
        current_x = x;
        while((*ch != '\0') && (current_x <= (OLED_WIDTH - 8)) && ((uint16_t)row_len + 8 <= OLED_TX_DATA_MAX_SIZE))
		{
            c = (uint8_t)(*ch);
            if((c < ' ') || (c > '~'))
			{
                c = ' ';
            }
            c = (uint8_t)(c - ' ');

            for(i = 0; i < 8; i++)
			{
                upper_buf[row_len + i] = F8X16[(c * 16) + i];
                lower_buf[row_len + i] = F8X16[(c * 16) + 8 + i];
            }

            row_len = (uint8_t)(row_len + 8);
            current_x = (uint8_t)(current_x + 8);
            ch++;
        }

        if(row_len > 0)
		{
            if(!oled_set_position_buf(x, y) || !oled_write_data_buf(upper_buf, row_len))
			{
                return 0;
            }
            if(!oled_set_position_buf(x, (uint8_t)(y + 1)) || !oled_write_data_buf(lower_buf, row_len))
			{
                return 0;
            }
        }

        if(*ch != '\0')
		{
            x = 0;
            y = (uint8_t)(y + 2);
        }
    }

    return 1;
}


// 在OLED指定位置显示16px ASCII字符串。
uint8_t OLED_ShowStr(uint8_t x, uint8_t y, char *ch)
{
    if(!ch)
	{
        return 0;
    }

    return oled_show_ascii_16(x, y, ch);
}

/* 清空OLED显存。 */
static void oled_clear(void)
{
    static const uint8_t zeros[OLED_TX_DATA_MAX_SIZE] = {0};
    uint8_t pos_cmds[3];
    uint8_t i;

    for (i = 0; i < 4; i++)
    {
        pos_cmds[0] = (uint8_t)(0xB0U + i);
        pos_cmds[1] = 0x10U;
        pos_cmds[2] = 0x00U;
        oled_write_cmd_buf(pos_cmds, sizeof(pos_cmds));
        oled_write_data_buf(zeros, OLED_TX_DATA_MAX_SIZE);
    }
}

/* 初始化SSD1306。 */
void OLED_Init(void)
{
    delay_ms(100);
    s_oled_available = 1;

    oled_write_cmd_buf(initcmd1, sizeof(initcmd1));
    if (!s_oled_available)
	{
        return;
    }

    oled_clear();
    oled_set_position_buf(0, 0);
}
