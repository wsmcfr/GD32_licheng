/************************************************************
 * 版权：2025CIMC Copyright。
 * 文件：BootConfig.c
 * 作者: Jialei Zhao
 * 平台: 2025CIMC IHD-V04
 * 版本: Jialei Zhao     2026/2/4     V0.01    original
************************************************************/


/************************* 头文件 *************************/

#include "BootConfig.h"

/************************* 宏定义 *************************/


/************************ 变量定义 ************************/

/************************ 函数定义 ************************/

/************************************************************
 * Function :       bootloader_config_init
 * Comment  :       生成一套 BootLoader 参数区默认内容。
 *                  这个函数本身并不会把数据写回 Flash，只是把默认值写到调用者
 *                  提供的几个结构体中，后续是否写入参数区由上层决定。
 *                  当前工程的启动主流程没有直接调用它，它更适合用于：
 *                  1. 首次烧录后的参数区初始化；
 *                  2. 产测阶段写入默认设备信息；
 *                  3. 需要恢复默认配置时重新生成参数镜像。
 * Parameter:       param      主参数区结构体输出指针。
 * Parameter:       mylog      升级日志区结构体输出指针。
 * Parameter:       userconfig 用户配置区结构体输出指针。
 * Parameter:       calibdata  校准区结构体输出指针。
 * Return   :       无返回值。
 * Author   :       Jialei Zhao
 * Date     :       2026-2-4 V0.1 original
************************************************************/
void bootloader_config_init(BootParam_t* param ,
	UpdateLog_t* mylog ,
	UserConfig_t* userconfig ,
	CalibData_t* calibdata)
{
	/* ================= 临时结构体变量 ================= */
	BootParam_t  tmp_param = { 0 };
	UpdateLog_t  tmp_log = { 0 };
	UserConfig_t tmp_usercfg = { 0 };
	CalibData_t  tmp_calib = { 0 };

	/* ================= 临时结构体初始值赋值 ================= */

	/* 1. 初始化主参数区。这里定义了 BootLoader 与 App 默认共享的基础约定。 */
	tmp_param.magicWord = 0x5AA5C33C;
	tmp_param.version = 0x0001;
	tmp_param.structSize = 256;
	tmp_param.updateMode = 0x01;
	tmp_param.appStartAddr = 0x0800D000;
	tmp_param.appEntryAddr = 0x0800D000;

	/*
	 * 默认直接读取 App 区向量表的第 0 项作为初始栈顶。
	 * 这要求 0x0800D000 处已经放着一个合法 App；如果用于首次初始化空片，
	 * 上层应自行决定是否保留该值或在后续重新刷新。
	 */
	tmp_param.appStackAddr = *(uint32_t*)0x0800D000;
	tmp_param.bootVersion = 0x01;
	tmp_param.bootSize = 4096;
	tmp_param.backupAddr = 0x0800C100;
	tmp_param.backupSize = 256;

	/* 写入默认设备信息，用于后续识别板卡与型号。 */
	memcpy(tmp_param.deviceID , "202601301528" , 12);
	memcpy(tmp_param.productModel , "000000000001" , 12);
	memcpy(tmp_param.serialNumber , "100000000000" , 12);

	tmp_param.hwVersion = 1;
	tmp_param.cpuID = 1;
	tmp_param.flashSize = 0x400;
	tmp_param.ramSize = 0x2F;
	tmp_param.clockFreq = 240000000;
	tmp_param.tailMagic = 0xA5A5C3C3;

	/* 2. 初始化升级日志区默认值。 */
	tmp_log.mode = 1;

	/* 3. 初始化用户配置区默认值，当前默认串口波特率与 BootLoader/App 约定保持一致。 */
	tmp_usercfg.uart_baudrate = 460800;
	tmp_usercfg.uart_stopbit = 1;

	/* 4. 初始化校准区的识别魔术字，便于后续区分“未初始化”和“已写入校准数据”。 */
	tmp_calib.calib_magic = 0xCAC0FFEE;

	/* ================= 输出到形参指针 ================= */
	*param = tmp_param;
	*mylog = tmp_log;
	*userconfig = tmp_usercfg;
	*calibdata = tmp_calib;
}

/****************************End*****************************/
