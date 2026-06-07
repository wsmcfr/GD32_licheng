#include "scheduler.h"
#include <rt_sys.h>

/*
 * Arm C 库 retarget 桩：禁用 semihosting，所有标准 I/O 直接丢弃。
 * 不加这些桩，C 库的 _sys_open/_sys_write 默认执行 BKPT 0xAB，
 * 脱离调试器运行时会触发 HardFault，表现为 App 无任何输出。
 */

#define APP_STDIN_HANDLE        ((FILEHANDLE)0)
#define APP_STDOUT_HANDLE       ((FILEHANDLE)1)
#define APP_STDERR_HANDLE       ((FILEHANDLE)2)

#if defined(__ARMCC_VERSION)
/* 告知 ArmClang C 库当前固件不使用 semihosting */
__asm(".global __use_no_semihosting\n");
#endif

/* 判断 C 库传入的文件名是否为三个标准流之一，返回对应句柄或 -1 */
static FILEHANDLE app_stdio_name_to_handle(const char *name)
{
    if(name == __stdin_name)  { return APP_STDIN_HANDLE;  }
    if(name == __stdout_name) { return APP_STDOUT_HANDLE; }
    if(name == __stderr_name) { return APP_STDERR_HANDLE; }
    return (FILEHANDLE)-1;
}

FILEHANDLE _sys_open(const char *name, int openmode)
{
    (void)openmode;
    return app_stdio_name_to_handle(name);
}

int _sys_close(FILEHANDLE fh)
{
    (void)fh;
    return 0;
}

// stdout/stderr 数据直接丢弃，避免向 RS485 写入非协议文本
int _sys_write(FILEHANDLE fh, const unsigned char *buf, unsigned len, int mode)
{
    (void)mode;
    (void)buf;

    if((fh != APP_STDOUT_HANDLE) && (fh != APP_STDERR_HANDLE)) {
        return (int)len;
    }

    return 0;
}

// 无法读取标准输入，返回 len 表示未读到任何字节
int _sys_read(FILEHANDLE fh, unsigned char *buf, unsigned len, int mode)
{
    (void)fh;
    (void)buf;
    (void)mode;
    return (int)len;
}

// stdin/stdout/stderr 按终端处理，其他句柄不是终端
int _sys_istty(FILEHANDLE fh)
{
    if((fh == APP_STDIN_HANDLE) || (fh == APP_STDOUT_HANDLE) || (fh == APP_STDERR_HANDLE)) {
        return 1;
    }
    return 0;
}

int _sys_seek(FILEHANDLE fh, long pos)
{
    (void)fh;
    (void)pos;
    return -1;
}

int _sys_ensure(FILEHANDLE fh)
{
    (void)fh;
    return 0;
}

long _sys_flen(FILEHANDLE fh)
{
    (void)fh;
    return -1L;
}

int _sys_tmpnam2(char *name, int sig, unsigned maxlen)
{
    (void)name;
    (void)sig;
    (void)maxlen;
    return -1;
}

char *_sys_command_string(char *cmd, int len)
{
    if((cmd != NULL) && (len > 0)) {
        cmd[0] = '\0';
    }
    return cmd;
}

void _ttywrch(int ch)
{
    (void)ch;
}

// 裸机固件不返回到宿主环境，死循环保留现场
void _sys_exit(int returncode)
{
    (void)returncode;
    while(1) {
    }
}

/* 固件主入口：system_init() 初始化所有外设和任务表，然后循环运行调度器 */
int main(void)
{
	system_init();
    while(1) {
        scheduler_run();
    }
}
