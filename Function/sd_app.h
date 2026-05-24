#ifndef __SD_APP_H_
#define __SD_APP_H_

#include "system_all.h"

#ifndef SD_FATFS_DEMO_ENABLE
#define SD_FATFS_DEMO_ENABLE (0U)
#endif

/*
 * 函数作用：
 *   打开 SDIO 中断，为后续 FATFS 访问做准备。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   无返回值。
 */
void sd_fatfs_init(void);

/*
 * 函数作用：
 *   标记 SD/FatFs 运行时需要在下次访问前重新执行轻量初始化。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   无返回值。
 * 说明：
 *   深睡唤醒后 SDIO/SD 卡资源已经被关闭，但普通运行态未必马上访问 SD 卡。
 *   通过该接口仅设置待恢复标志，可把 SDIO 中断恢复延后到真正访问前。
 */
void sd_fatfs_mark_resume_required(void);

/*
 * 函数作用：
 *   在需要访问 SD/FatFs 前确保轻量初始化已经完成。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   无返回值。
 */
void sd_fatfs_ensure_ready(void);

/*
 * 函数作用：
 *   执行 SD 卡初始化、文件读写和长文件名验证示例。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   无返回值；测试过程通过调试串口输出。
 */
void sd_fatfs_test(void);

#endif /* __SD_APP_H_ */
