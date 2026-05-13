/************************************************************
 * 版权：2025CIMC Copyright。 
 * 文件：Headerfiles.h
 * 作者: Lingyu Meng
 * 平台: 2025 CIMC IHD V04
 * 版本: Lingyu Meng     2023/2/16     V0.01    original
************************************************************/

#ifndef __HEADERFILES_H
#define __HEADERFILES_H

/************************* 头文件 *************************/

/*
 * 这个头文件是 BootLoader 工程的“总入口头文件”。
 * 自写模块通常只需要包含它，就能同时拿到：
 * 1. 芯片寄存器与外设定义；
 * 2. SysTick 延时接口；
 * 3. C 标准库里 printf / memcpy / 基本整数类型等常用能力；
 * 4. BootLoader 主流程函数声明。
 *
 * 这样做的目的，是减少各个 .c 文件里重复 include 的数量，让工程入口更统一。
 */
#include "gd32f4xx.h"
#include "gd32f4xx_libopt.h"
#include "systick.h"
#include <stdio.h>
#include <stdint.h>
#include "string.h"
#include "Function.h"     // 执行函数

#endif

/****************************End*****************************/

