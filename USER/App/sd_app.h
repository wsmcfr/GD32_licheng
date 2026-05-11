#ifndef __SD_APP_H_
#define __SD_APP_H_

#include "system_all.h"

#ifndef SD_FATFS_DEMO_ENABLE
#define SD_FATFS_DEMO_ENABLE (1U)
#endif

/*
 * 函数作用：
 *   打开 SDIO 中断，为后续 FATFS 访问做准备。
 */
void sd_fatfs_init(void);

/*
 * 函数作用：
 *   执行 SD 卡初始化、文件读写和长文件名验证示例。
 */
void sd_fatfs_test(void);

#endif /* __SD_APP_H_ */
