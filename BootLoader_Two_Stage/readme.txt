# CIMC中国智能制造挑战赛-工业嵌入式系统开发赛项

# Program：BootLoader_Two_Stage

## 程序简介
- 工程名称：GD32F470 DEMO 程序模板
- 实验平台: CIMC GD32F470 Development Kit V2.0
- MDK版本：5.25
Program：BootLoader_Two_Stage
## 板载资源

 - GD32F470VET6 MCU
 
 
## 功能简介

进行串口升级程序


## 实验操作

先烧录 BootLoader 程序 ， 后烧录 App程序 然后 通过串口0发送数据可以进行App程序的升级 ， 升级数据内存最大 12kb
文件头的魔术值为0x5AA5C33C	，不一致无法进行升级

升级后的版本 与 原始的版本 LED灯亮的不一样，然后就是原始版本可以进行串口的信息打印 hello world

LED.bin 和 LED10.bin  魔术值为：0x5AA5C33C 版本号 为  0x00  0x1000

LED111.bin的魔术值为0x6AA6C33C 无法进行升级

## 引脚分配

LED1	----	PA0
LED2	----	PA1
LED3	----	PA4
LED4	----	PA5
LED5	----	PA6
LED6	----	PA7

## 程序版本

- 程序版本：V0.1
- 发布日期：2025-03-22

## 联系我们

- Copyright   : CIMC中国智能制造挑战赛
- Author      ：Lingyu Meng
- Website     ：www.siemenscup-cimc.org.cn
- Phone       ：15801122380

## 声明

**严禁商业用途，仅供学习使用。 **


## 目录结构

├─27_0_BootLoader					BootLoader程序
│  ├─01 Readme
│  ├─CMSIS
│  ├─Function
│  ├─HardWare
│  │  ├─BootLoader
│  │  ├─LED
│  │  ├─ROM
│  │  └─USART
│  ├─HeaderFiles
│  ├─Library
│  │  ├─GD32F4xx_usb_library
│  ├─project
│  ├─Startup
│  └─User
└─27_1_App							App程序
    ├─01 Readme
    ├─CMSIS
    ├─Function
    ├─HardWare
    │  ├─Config
    │  ├─LED
    │  ├─ROM
    │  └─USART
    ├─HeaderFiles
    ├─Library
    ├─project
    ├─Startup
    └─User

