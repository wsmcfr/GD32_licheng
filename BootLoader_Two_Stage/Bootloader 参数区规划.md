# Bootloader 参数区规划

参数区总大小: 4KB (4096字节)
起始地址: 0x0800C000
结束地址: 0x0800CFFF

═══════════════════════════════════════════════════════════
区域划分:
  ├─ 主参数区          256字节  (0x0800C000 - 0x0800C0FF)
  ├─ 备份参数区        256字节  (0x0800C100 - 0x0800C1FF)
  ├─ 升级日志区       1024字节  (0x0800C200 - 0x0800C5FF)
  ├─ 用户配置区        512字节  (0x0800C600 - 0x0800C7FF)
  ├─ 校准数据区        512字节  (0x0800C800 - 0x0800C9FF)
  └─ 预留扩展区       1536字节  (0x0800CA00 - 0x0800CFFF)
═══════════════════════════════════════════════════════════

```
// 参数区地址定义
#define PARAM_AREA_BASE         0x0800C000  // 参数区起始地址（4KB）
#define PARAM_AREA_SIZE         0x00001000  // 4096字节

#define PARAM_MAIN_ADDR         0x0800C000  // 主参数区（256B）
#define PARAM_BACKUP_ADDR       0x0800C100  // 备份参数区（256B）
#define LOG_AREA_ADDR           0x0800C200  // 日志区（1KB）
#define USER_CONFIG_ADDR        0x0800C600  // 用户配置区（512B）
#define CALIB_DATA_ADDR         0x0800C800  // 校准数据区（512B）
#define EXTEND_AREA_ADDR        0x0800CA00  // 扩展区（1536B）

#define PARAM_MAGIC_WORD        0x5AA5C33C
#define PARAM_TAIL_MAGIC        0xA5A5C3C3
#define CALIB_MAGIC_WORD        0xCAC0FFEE
```



## 主参数区结构

```
/**
 * @brief Bootloader主参数区结构体
 * @note  存储在0x0800C000，大小256字节
 */
typedef struct __attribute__((packed)) {
    // ============ [0-15] 基础标识 (16字节) ============
    uint32_t magicWord;         // [0-3]   魔术字: 0x5AA5C33C
    uint16_t version;           // [4-5]   参数版本: 0x0100 (v1.0)
    uint16_t structSize;        // [6-7]   结构体大小: 256
    uint32_t buildDate;         // [8-11]  参数创建日期(BCD)
    uint32_t reserved0;         // [12-15] 保留
    
    // ============ [16-31] 升级控制 (16字节) ============
    uint8_t  updateFlag;        // [16]    升级标志: 0x5A=升级, 0x00=正常 ★
    uint8_t  updateMode;        // [17]    升级模式: 0x01=串口
    uint8_t  updateStatus;      // [18]    升级状态
    uint8_t  updateProgress;    // [19]    升级进度: 0-100
    uint32_t updateCount;       // [20-23] 升级次数累计
    uint32_t lastUpdateTime;    // [24-27] 最后升级时间戳
    uint32_t reserved1;         // [28-31] 保留
    
    // ============ [32-63] App固件信息 (32字节) ============
    uint32_t appSize;           // [32-35] App大小(字节)
    uint32_t appCRC32;          // [36-39] App CRC32校验值 ★
    uint32_t appVersion;        // [40-43] App版本(主.次.补丁.构建)
    uint32_t appBuildDate;      // [44-47] App编译日期
    uint32_t appStartAddr;      // [48-51] App起始地址: 0x0800D000
    uint32_t appEntryAddr;      // [52-55] App入口地址
    uint32_t appStackAddr;      // [56-59] App栈地址(MSP)
    uint32_t reserved2;         // [60-63] 保留
    
    // ============ [64-79] Bootloader信息 (16字节) ============
    uint32_t bootVersion;       // [64-67] Bootloader版本
    uint32_t bootCRC32;         // [68-71] Bootloader CRC32
    uint32_t bootSize;          // [72-75] Bootloader大小
    uint32_t reserved3;         // [76-79] 保留
    
    // ============ [80-111] 系统状态 (32字节) ============
    uint32_t runTimestamp;      // [80-83]  最后运行时间
    uint32_t resetCount;        // [84-87]  总复位次数
    uint16_t lastResetReason;   // [88-89]  最后复位原因
    uint16_t bootFailCount;     // [90-91]  启动失败次数 ★
    uint32_t totalRuntime;      // [92-95]  总运行时间(秒)
    uint32_t wdtResetCount;     // [96-99]  看门狗复位次数
    uint32_t hardFaultCount;    // [100-103] HardFault次数
    uint32_t lastErrorCode;     // [104-107] 最后错误码
    uint32_t reserved4;         // [108-111] 保留
    
    // ============ [112-143] 备份固件信息 (32字节) ============
    uint32_t backupFlag;        // [112-115] 备份标志: 0xAA=有效
    uint32_t backupAddr;        // [116-119] 备份地址
    uint32_t backupSize;        // [120-123] 备份大小
    uint32_t backupCRC32;       // [124-127] 备份CRC32
    uint32_t backupVersion;     // [128-131] 备份版本号
    uint32_t backupDate;        // [132-135] 备份时间
    uint32_t reserved5[2];      // [136-143] 保留
    
    // ============ [144-159] 安全相关 (16字节 - 保留) ============
    uint32_t securityFlag;      // [144-147] 安全标志
    uint32_t encryptKey;        // [148-151] 加密密钥索引
    uint32_t authCode;          // [152-155] 认证码
    uint32_t reserved6;         // [156-159] 保留
    
    // ============ [160-207] 设备信息 (48字节) ============
    uint8_t  deviceID[16];      // [160-175] 设备唯一ID
    uint8_t  productModel[16];  // [176-191] 产品型号
    uint8_t  serialNumber[16];  // [192-207] 序列号
    
    // ============ [208-239] 硬件配置 (32字节) ============
    uint32_t hwVersion;         // [208-211] 硬件版本
    uint32_t cpuID;             // [212-215] CPU ID
    uint16_t flashSize;         // [216-217] Flash容量(KB)
    uint16_t ramSize;           // [218-219] RAM容量(KB)
    uint32_t clockFreq;         // [220-223] 时钟频率(Hz)
    uint32_t reserved7[4];      // [224-239] 保留
    
    // ============ [240-255] 校验与结束 (16字节) ============
    uint32_t reserved8[2];      // [240-247] 保留
    uint32_t paramCRC32;        // [248-251] 整个参数区CRC32 ★
    uint32_t tailMagic;         // [252-255] 尾部魔术字: 0xA5A5C3C3
    
} BootParam_t;  // 总大小: 256字节
```



## 完整参数区布局

```
═══════════════════════════════════════════════════════════════════
区域1: 主参数区 (0x0800C000 - 0x0800C0FF, 256字节)
═══════════════════════════════════════════════════════════════════

偏移    | 字段名              | 大小 | 典型值        | 说明
────────┼────────────────────┼──────┼───────────────┼─────────────
0x0000  | magicWord          | 4B   | 0x5AA5C33C    | 有效性标识
0x0004  | version            | 2B   | 0x0100        | v1.0
0x0006  | structSize         | 2B   | 0x0100        | 256字节
0x0008  | buildDate          | 4B   | 0x20260129    | 2026-01-29
0x000C  | reserved0          | 4B   | 0x00000000    |
────────┼────────────────────┼──────┼───────────────┼─────────────
0x0010  | updateFlag         | 1B   | 0x5A          | ★升级标志
0x0011  | updateMode         | 1B   | 0x01          | 串口
0x0012  | updateStatus       | 1B   | 0x07          | 成功
0x0013  | updateProgress     | 1B   | 0x64          | 100%
0x0014  | updateCount        | 4B   | 0x0000000A    | 10次
0x0018  | lastUpdateTime     | 4B   | 0x65B9A8C0    | Unix时间
0x001C  | reserved1          | 4B   | 0x00000000    |
────────┼────────────────────┼──────┼───────────────┼─────────────
0x0020  | appSize            | 4B   | 0x0000D000    | 53248
0x0024  | appCRC32           | 4B   | 0x1A2B3C4D    | ★CRC校验
0x0028  | appVersion         | 4B   | 0x02010000    | v2.1.0.0
0x002C  | appBuildDate       | 4B   | 0x20260129    |
0x0030  | appStartAddr       | 4B   | 0x08010000    |
0x0034  | appEntryAddr       | 4B   | 0x08010400    |
0x0038  | appStackAddr       | 4B   | 0x20020000    | MSP初值
0x003C  | reserved2          | 4B   | 0x00000000    |
────────┼────────────────────┼──────┼───────────────┼─────────────
0x0040  | bootVersion        | 4B   | 0x01000000    | Boot v1.0
0x0044  | bootCRC32          | 4B   | 0xABCDEF12    |
0x0048  | bootSize           | 4B   | 0x00004000    | 16KB
0x004C  | reserved3          | 4B   | 0x00000000    |
────────┼────────────────────┼──────┼───────────────┼─────────────
0x0050  | runTimestamp       | 4B   | 0x65B9A8C0    |
0x0054  | resetCount         | 4B   | 0x00000032    | 50次
0x0058  | lastResetReason    | 2B   | 0x0001        | 软复位
0x005A  | bootFailCount      | 2B   | 0x0000        | ★失败计数
0x005C  | totalRuntime       | 4B   | 0x000F4240    | 1000000秒
0x0060  | wdtResetCount      | 4B   | 0x00000005    | 5次
0x0064  | hardFaultCount     | 4B   | 0x00000000    |
0x0068  | lastErrorCode      | 4B   | 0x00000000    |
0x006C  | reserved4          | 4B   | 0x00000000    |
────────┼────────────────────┼──────┼───────────────┼─────────────
0x0070  | backupFlag         | 4B   | 0x000000AA    | 有备份
0x0074  | backupAddr         | 4B   | 0x08060000    |
0x0078  | backupSize         | 4B   | 0x0000C800    | 51200
0x007C  | backupCRC32        | 4B   | 0x9876FEDC    |
0x0080  | backupVersion      | 4B   | 0x01090000    | v1.9.0.0
0x0084  | backupDate         | 4B   | 0x20260115    |
0x0088  | reserved5[2]       | 8B   | 0x00...       |
────────┼────────────────────┼──────┼───────────────┼─────────────
0x0090  | securityFlag       | 4B   | 0x00000000    |
0x0094  | encryptKey         | 4B   | 0x00000000    |
0x0098  | authCode           | 4B   | 0x00000000    |
0x009C  | reserved6          | 4B   | 0x00000000    |
────────┼────────────────────┼──────┼───────────────┼─────────────
0x00A0  | deviceID[16]       | 16B  | "GD32F470..."  | 唯一ID
0x00B0  | productModel[16]   | 16B  | "GD32F470VG"   | 型号
0x00C0  | serialNumber[16]   | 16B  | "SN2026012900" | 序列号
────────┼────────────────────┼──────┼───────────────┼─────────────
0x00D0  | hwVersion          | 4B   | 0x02000000    | HW v2.0
0x00D4  | cpuID              | 4B   | 0x413FC230    | Cortex-M4
0x00D8  | flashSize          | 2B   | 0x0200        | 512KB
0x00DA  | ramSize            | 2B   | 0x0080        | 128KB
0x00DC  | clockFreq          | 4B   | 0x0EE6B280    | 250MHz
0x00E0  | reserved7[4]       | 16B  | 0x00...       |
────────┼────────────────────┼──────┼───────────────┼─────────────
0x00F0  | reserved8[2]       | 8B   | 0x00...       |
0x00F8  | paramCRC32         | 4B   | 0xXXXXXXXX    | ★整体校验
0x00FC  | tailMagic          | 4B   | 0xA5A5C3C3    | 尾标识


═══════════════════════════════════════════════════════════════════
区域2: 备份参数区 (0x0800C100 - 0x0800C1FF, 256字节)
═══════════════════════════════════════════════════════════════════
与主参数区结构完全相同，用于冗余备份
每次写主参数区后，自动同步写入备份区


═══════════════════════════════════════════════════════════════════
区域3: 升级日志区 (0x0800C200 - 0x0800C5FF, 1024字节)
═══════════════════════════════════════════════════════════════════

/**
 * @brief 单条升级日志（32字节）
 */
typedef struct __attribute__((packed)) {
    uint32_t timestamp;         // 升级时间戳
    uint32_t oldVersion;        // 旧版本
    uint32_t newVersion;        // 新版本
    uint32_t newSize;           // 新固件大小
    uint32_t newCRC32;          // 新固件CRC
    uint8_t  status;            // 状态: 0x00=成功, 0xFF=失败
    uint8_t  mode;              // 模式: 0x01=串口, 0x02=CAN
    uint16_t duration;          // 耗时(秒)
    uint32_t errorCode;         // 错误码(失败时)
    uint32_t reserved;          // 保留
} UpdateLog_t;  // 32字节

// 日志区可存储32条记录 (1024 / 32 = 32)
// 采用循环覆盖方式，最多保留32次升级历史

偏移      | 日志序号 | 说明
──────────┼─────────┼──────────────────────
0x0800C200| Log #0  | 最早的记录
0x0800C220| Log #1  |
0x0800C240| Log #2  |
...       | ...     |
0x0800C5C0| Log #30 |
0x0800C5E0| Log #31 | 最新的记录


═══════════════════════════════════════════════════════════════════
区域4: 用户配置区 (0x0800C600 - 0x0800C7FF, 512字节)
═══════════════════════════════════════════════════════════════════

/**
 * @brief 用户配置参数（512字节）
 */
typedef struct __attribute__((packed)) {
    // [0-31] 通信配置
    uint32_t uart_baudrate;     // 串口波特率
    uint8_t  uart_parity;       // 校验位
    uint8_t  uart_stopbit;      // 停止位
    uint16_t reserved_uart;
    uint32_t can_baudrate;      // CAN波特率 - 保留
    uint32_t eth_ip;            // 以太网IP - 保留
    uint32_t eth_mask;          // 子网掩码 - 保留
    uint32_t eth_gateway;       // 网关 - 保留
    uint32_t reserved_comm[2];
    
    // [32-63] 功能开关
    uint32_t feature_flags;     // 功能标志位 - 保留
    uint32_t debug_level;       // 调试级别 - 保留
    uint32_t watchdog_timeout;  // 看门狗超时(ms) - 保留
    uint32_t reserved_feat[5];
    
    // [64-127] 定时器配置
    uint32_t timer_intervals[16]; // 16个定时器周期
    
    // [128-255] IO配置
    uint8_t  gpio_config[128];   // GPIO配置表 - 保留
    
    // [256-383] 自定义参数
    uint8_t  user_data[128];     // 用户自定义数据 - 保留
    
    // [384-511] 校验与保留
    uint8_t  reserved[124];
    uint32_t configCRC32;        // 配置区CRC32
} UserConfig_t;  // 512字节


═══════════════════════════════════════════════════════════════════
区域5: 校准数据区 (0x0800C800 - 0x0800C9FF, 512字节)
═══════════════════════════════════════════════════════════════════

/**
 * @brief 出厂校准数据（512字节）  - 整个区域都做保留
 */
typedef struct __attribute__((packed)) {
    // [0-15] 标识
    uint32_t calib_magic;       // 校准魔术字: 0xCAC0FFEE
    uint32_t calib_date;        // 校准日期
    uint32_t calib_version;     // 校准版本
    uint32_t reserved0;
    
    // [16-47] ADC校准
    uint16_t adc_offset[16];    // ADC偏移校准
    
    // [48-79] DAC校准
    uint16_t dac_gain[16];      // DAC增益校准
    
    // [80-143] 温度校准
    int16_t  temp_curve[32];    // 温度曲线校准点
    
    // [144-207] 电压校准
    float    voltage_k[16];     // 电压斜率
    
    // [208-271] 电流校准
    float    current_k[16];     // 电流斜率
    
    // [272-507] 保留
    uint8_t  reserved[236];
    
    // [508-511] 校验
    uint32_t calibCRC32;        // 校准数据CRC32
} CalibData_t;  // 512字节


═══════════════════════════════════════════════════════════════════
区域6: 预留扩展区 (0x0800CA00 - 0x0800CFFF, 1536字节)
═══════════════════════════════════════════════════════════════════

可用于:
  - 增加更多日志记录
  - 存储更大的配置文件
  - 黑匣子记录(故障前数据)
  - OTA证书存储
  - 加密密钥链
  - ...


═══════════════════════════════════════════════════════════════════
总结: 4KB参数区完整布局
═══════════════════════════════════════════════════════════════════

0x0800C000  ┌────────────────────────────────────┐
            │ 主参数区 (256B)                     │ ◄── 最常访问
            │ - 升级控制                          │
            │ - App信息                           │
            │ - 系统状态                          │
0x0800C100  ├────────────────────────────────────┤
            │ 备份参数区 (256B)                   │ ◄── 冗余保护
0x0800C200  ├────────────────────────────────────┤
            │ 升级日志区 (1KB)                    │ ◄── 历史记录
            │ - 32条升级记录                      │
0x0800C600  ├────────────────────────────────────┤
            │ 用户配置区 (512B)                   │ ◄── 可配置
            │ - 通信参数                          │
            │ - 功能开关                          │
0x0800C800  ├────────────────────────────────────┤
            │ 校准数据区 (512B)                   │ ◄── 出厂固化（保留）
            │ - ADC/DAC校准                       │
            │ - 温度/电压校准                     │
0x0800CA00  ├────────────────────────────────────┤
            │ 预留扩展区 (1536B)                  │ ◄── 未来扩展
            │                                    │
0x0800CFFF  └────────────────────────────────────┘
```



