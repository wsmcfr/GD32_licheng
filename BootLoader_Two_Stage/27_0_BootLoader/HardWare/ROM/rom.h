/************************************************************
 * 版权：2025CIMC Copyright。
 * 文件：rom.h
 * 作者: Jialei Zhao
 * 平台: 2025CIMC IHD-V04
 * 版本: Jialei Zhao     2026/2/4     V0.01    original
************************************************************/
#ifndef __ROM_H__
#define __ROM_H__

/************************* 头文件 *************************/

#include "HeaderFiles.h"

/************************* 宏定义 *************************/


/************************ 变量定义 ************************/

/************************ 函数定义 ************************/

/*
 * 下面这组接口是 BootLoader 对 GD32 FMC 内部 Flash 编程接口的轻量封装。
 * 这样做的目的，是把“解锁 / 擦除 / 写入 / 清标志 / 上锁”这些重复动作集中到一处，
 * 让上层升级流程只关注“要擦哪里、要写什么”。
 */

/*
 * 函数作用：
 *   按 4KB 页粒度擦除指定地址所在的内部 Flash 页。
 * 参数说明：
 *   addr：要擦除的页起始地址。调用者需要保证该地址按页对齐，并且位于允许擦写的 Flash 区域。
 * 返回值说明：
 *   无返回值。
 */
void internal_flash_erase(uint32_t addr);

/*
 * 函数作用：
 *   按芯片扇区粒度擦除指定地址所在的内部 Flash 扇区。
 * 参数说明：
 *   addr：目标扇区中的任意地址，底层库会按该地址所属扇区执行擦除。
 * 返回值说明：
 *   无返回值。
 */
void internal_flash_earse_sector(uint32_t addr);

/*
 * 函数作用：
 *   向内部 Flash 指定地址写入一个 16 位半字。
 * 参数说明：
 *   addr：写入地址，应满足半字写入对齐要求。
 *   data：要写入的半字数据。
 * 返回值说明：
 *   无返回值。
 */
void internal_flash_write(uint32_t addr , uint16_t data);

/*
 * 函数作用：
 *   向内部 Flash 指定地址写入一个 32 位字。
 * 参数说明：
 *   addr：写入地址，应满足字写入对齐要求。
 *   data：要写入的 32 位数据。
 * 返回值说明：
 *   无返回值。
 */
void internal_flash_write_word(uint32_t addr , uint32_t data);

/*
 * 函数作用：
 *   以半字数组形式连续写入一段内部 Flash。
 * 参数说明：
 *   addr：起始写入地址。
 *   data：待写入的半字数组首地址。
 *   len：半字个数，不是字节数。
 * 返回值说明：
 *   无返回值。
 */
void internal_flash_write_str(uint32_t addr , uint16_t* data , uint16_t len);

/*
 * 函数作用：
 *   以字节流形式连续写入一段内部 Flash。
 * 参数说明：
 *   addr：起始写入地址。
 *   data：待写入的字节缓冲区首地址。
 *   len：写入字节数。
 * 返回值说明：
 *   无返回值。
 */
void internal_flash_write_str_Char(uint32_t addr , uint8_t* data , uint32_t len);

/*
 * 函数作用：
 *   从内部 Flash 指定地址读取一个半字。
 * 参数说明：
 *   addr：待读取地址。
 * 返回值说明：
 *   返回该地址处的 16 位数据。
 */
uint16_t internal_flash_read(uint32_t addr);

/*
 * 函数作用：
 *   从内部 Flash 指定地址读取一个字节。
 * 参数说明：
 *   addr：待读取地址。
 * 返回值说明：
 *   返回该地址处的 8 位数据。
 */
uint8_t internal_flash_read_Char(uint32_t addr);

/*
 * 函数作用：
 *   从内部 Flash 指定地址读取一个 32 位字。
 * 参数说明：
 *   addr：待读取地址。
 * 返回值说明：
 *   返回该地址处的 32 位数据。
 */
uint32_t internal_flash_read_word(uint32_t addr);

#endif // !__ROM_H__

/****************************End*****************************/
