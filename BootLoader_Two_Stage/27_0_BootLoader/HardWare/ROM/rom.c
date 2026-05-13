/************************************************************
 * 版权：2025CIMC Copyright。
 * 文件：usart.c
 * 作者: Jialei Zhao
 * 平台: 2025CIMC IHD-V04
 * 版本: Jialei Zhao     2026/2/4     V0.01    original
************************************************************/


/************************* 头文件 *************************/

#include "rom.h"

/************************* 宏定义 *************************/


/************************ 变量定义 ************************/


/************************ 函数定义 ************************/

/*
 * 这个文件专门封装 GD32 FMC 的基础读写能力。
 * BootLoader 在搬运 App 时会频繁调用这里的接口，因此这里尽量保持“动作单一、
 * 调用稳定”，避免在业务层到处散落解锁和清标志代码。
 */

/************************************************************
 * Function :       internal_flash_erase
 * Comment  :       擦除指定地址所在的 4KB Flash 页。
 *                  BootLoader 当前把参数区和 App 区都按页来管理，因此这是主流程最常用的擦除接口。
 * Parameter:       addr 目标页起始地址，调用者需保证地址位于可擦写内部 Flash 中。
 * Return   :       无返回值。
 * Author   :       Jialei Zhao
 * Date     :       2026-2-4 V0.1 original
************************************************************/
void internal_flash_erase(uint32_t addr)
{
	/* 先解锁 FMC，否则后续擦写命令不会生效。 */
	fmc_unlock();

	/* 擦除前先清历史状态，避免上一次操作残留标志干扰本次判断。 */
	fmc_flag_clear(FMC_FLAG_END);
	fmc_flag_clear(FMC_FLAG_WPERR);
	fmc_flag_clear(FMC_FLAG_PGSERR);
	fmc_flag_clear(FMC_FLAG_PGMERR);

	/* 真正执行页擦除。 */
	fmc_page_erase(addr);

	/* 擦除结束后再次清标志，让下一次操作从干净状态开始。 */
	fmc_flag_clear(FMC_FLAG_END);
	fmc_flag_clear(FMC_FLAG_WPERR);
	fmc_flag_clear(FMC_FLAG_PGSERR);
	fmc_flag_clear(FMC_FLAG_PGMERR);

	/* 及时重新上锁，避免误写 Flash。 */
	fmc_lock();
}
/************************************************************
 * Function :       internal_flash_earse_sector
 * Comment  :       擦除指定地址所在的 Flash 扇区。
 *                  当前 BootLoader 主流程主要使用页擦除，这个接口属于保留能力。
 * Parameter:       addr 目标扇区内任意地址。
 * Return   :       无返回值。
 * Author   :       Jialei Zhao
 * Date     :       2026-2-4 V0.1 original
************************************************************/
void internal_flash_earse_sector(uint32_t addr)
{
	fmc_unlock();
	fmc_flag_clear(FMC_FLAG_END);
	fmc_flag_clear(FMC_FLAG_WPERR);
	fmc_flag_clear(FMC_FLAG_PGSERR);
	fmc_flag_clear(FMC_FLAG_PGMERR);

	/* 使用底层库按扇区执行整块擦除。 */
	fmc_sector_erase(addr);

	fmc_flag_clear(FMC_FLAG_END);
	fmc_flag_clear(FMC_FLAG_WPERR);
	fmc_flag_clear(FMC_FLAG_PGSERR);
	fmc_flag_clear(FMC_FLAG_PGMERR);

	fmc_lock();
}
/************************************************************
 * Function :       internal_flash_write
 * Comment  :       向内部 Flash 写入一个 16 位半字。
 * Parameter:       addr 写入地址。
 * Parameter:       data 要写入的半字数据。
 * Return   :       无返回值。
 * Author   :       Jialei Zhao
 * Date     :       2026-2-4 V0.1 original
************************************************************/
void internal_flash_write(uint32_t addr , uint16_t data)
{
	fmc_unlock();

	/* 半字写入适合保存短小配置字段。 */
	fmc_halfword_program(addr , data);

	fmc_flag_clear(FMC_FLAG_END);
	fmc_flag_clear(FMC_FLAG_WPERR);
	fmc_flag_clear(FMC_FLAG_PGSERR);
	fmc_flag_clear(FMC_FLAG_PGMERR);

	fmc_lock();
}
/************************************************************
 * Function :       internal_flash_write_word
 * Comment  :       向内部 Flash 写入一个 32 位字。
 * Parameter:       addr 写入地址。
 * Parameter:       data 要写入的 32 位数据。
 * Return   :       无返回值。
 * Author   :       Jialei Zhao
 * Date     :       2026-2-4 V0.1 original
************************************************************/
void internal_flash_write_word(uint32_t addr , uint32_t data)
{
	fmc_unlock();

	fmc_word_program(addr , data);

	fmc_flag_clear(FMC_FLAG_END);
	fmc_flag_clear(FMC_FLAG_WPERR);
	fmc_flag_clear(FMC_FLAG_PGSERR);
	fmc_flag_clear(FMC_FLAG_PGMERR);

	fmc_lock();
}

/************************************************************
 * Function :       internal_flash_write_str
 * Comment  :       以半字数组形式连续写入一段 Flash。
 * Parameter:       addr 起始写入地址。
 * Parameter:       data 半字数组首地址。
 * Parameter:       len 半字个数。
 * Return   :       无返回值。
 * Author   :       Jialei Zhao
 * Date     :       2026-2-4 V0.1 original
************************************************************/
void internal_flash_write_str(uint32_t addr , uint16_t* data , uint16_t len)
{
	fmc_unlock();

	/* 逐个半字编程，适合保存结构化参数。 */
	for (uint16_t i = 0; i < len; i++)
	{
		fmc_halfword_program(addr + i * 2 , data[i]);
	}

	fmc_flag_clear(FMC_FLAG_END);
	fmc_flag_clear(FMC_FLAG_WPERR);
	fmc_flag_clear(FMC_FLAG_PGSERR);
	fmc_flag_clear(FMC_FLAG_PGMERR);

	fmc_lock();
}
/************************************************************
 * Function :       internal_flash_write_str_Char
 * Comment  :       以字节流形式连续写入一段 Flash。
 *                  BootLoader 搬运 App 时正是通过这个接口把下载区的固件按块写入正式 App 区。
 * Parameter:       addr 起始写入地址。
 * Parameter:       data 字节流首地址。
 * Parameter:       len 写入字节数。
 * Return   :       无返回值。
 * Author   :       Jialei Zhao
 * Date     :       2026-2-4 V0.1 original
************************************************************/
void internal_flash_write_str_Char(uint32_t addr , uint8_t* data , uint32_t len)
{
	fmc_unlock();

	for (uint32_t i = 0; i < len; i++)
	{
		/* 按字节写入可以避免因为缓冲区未对齐而额外处理。 */
		fmc_byte_program(addr + i , data[i]);
		while(fmc_flag_get(FMC_FLAG_BUSY));
	}

	fmc_flag_clear(FMC_FLAG_END);
	fmc_flag_clear(FMC_FLAG_WPERR);
	fmc_flag_clear(FMC_FLAG_PGSERR);
	fmc_flag_clear(FMC_FLAG_PGMERR);

	fmc_lock();
}
/************************************************************
 * Function :       internal_flash_read
 * Comment  :       读取内部 Flash 中一个 16 位半字。
 * Parameter:       addr 待读取地址。
 * Return   :       返回该地址处的半字值。
 * Author   :       Jialei Zhao
 * Date     :       2026-2-4 V0.1 original
************************************************************/
uint16_t internal_flash_read(uint32_t addr)
{
	return *(volatile uint16_t*)(addr);
}
/************************************************************
 * Function :       internal_flash_read_Char
 * Comment  :       读取内部 Flash 中一个字节。
 * Parameter:       addr 待读取地址。
 * Return   :       返回该地址处的字节值。
 * Author   :       Jialei Zhao
 * Date     :       2026-2-4 V0.1 original
************************************************************/
uint8_t internal_flash_read_Char(uint32_t addr)
{
	return *(volatile uint8_t*)(addr);
}

/************************************************************
 * Function :       internal_flash_read_word
 * Comment  :       读取内部 Flash 中一个 32 位字。
 * Parameter:       addr 待读取地址。
 * Return   :       返回该地址处的 32 位值。
 * Author   :       Jialei Zhao
 * Date     :       2026-2-4 V0.1 original
************************************************************/
uint32_t internal_flash_read_word(uint32_t addr)
{
	return *(volatile uint32_t*)(addr);
}

/****************************End*****************************/
