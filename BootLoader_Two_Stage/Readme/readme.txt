# CIMC中国智能制造挑战赛-工业嵌入式系统开发赛项

# Program：BootLoader_Two_Stage

## 程序简介
- 工程名称：GD32F470 DEMO 程序模板
- 实验平台: CIMC GD32F470 Development Kit V2.0
- MDK版本：5.25


## 板载资源

 - GD32F470VET6 MCU
 
 
## 功能简介

程序模板，可以用来拷贝建立工程
利用GD32F470VET6单片机实现：程序的升级


## 实验操作

本实验需先烧录 BootLoader 程序，再烧录 App 程序。系统支持通过 串口0（USART0） 进行 App 程序升级，升级过程中可通过串口查看相关打印信息。升级数据的单次
最大内存大小为 12 KB。升级文件包含文件头校验字段，其中 魔术值为 0x5AA5C33C，用于判断升级文件是否合法；若魔术值与规定值不一致，BootLoader 将拒绝执行升
级。本实验采用 二段式升级机制，不具备版本回退功能。

测试文件中 LED.bin 与 LED10.bin 的魔术值均为 0x5AA5C33C，版本号分别为 0x00 和 0x1000，符合升级文件格式要求，因此可以正常进行升级。而 LED111.bin 
的魔术值不等于 0x5AA5C33C，不符合升级文件校验规则，系统在校验阶段将判定其为非法升级文件，因此无法执行升级操作。

注: LED.bin，LED111.bin，LED10.bin 的代码不具备升级功能，只是测试所用，如果想要测试那么应该确保A区程序应该是28_1_App项目的程序

App程序执行效果： LED1 间隔闪烁(500ms) , 间隔打印hello world(500ms)

LED.bin , LED10.bin 执行效果 四颗LED间隔1秒顺次点亮后熄灭，随后LED1间隔0.5秒闪烁。

## 引脚分配

App:
		LED1 <---> PA4
		usart0_tx <---> PA9
		usart0_rx <---> PA10

LED.bin、LED10.bin:
	
		LED1   <--->     PA4
		LED2   <--->     PA5
		LED3   <--->     PA6
		LED4   <--->     PA7

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

