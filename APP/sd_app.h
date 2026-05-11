#ifndef __SD_APP_H_
#define __SD_APP_H_

#include "stdint.h"

#ifndef SD_FATFS_DEMO_ENABLE
#define SD_FATFS_DEMO_ENABLE (1U)
#endif

void sd_fatfs_init(void);
void sd_fatfs_test(void);

#endif /* __SD_APP_H_ */
