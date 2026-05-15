#include "rs485_app.h"

__IO uint8_t rs485_rx_flag = 0U;
__IO uint16_t rs485_dma_length = 0U;
uint8_t rs485_dma_buffer[RS485_APP_BUFFER_SIZE] = {0U};

/*
 * 变量作用：
 *   RS485 任务层私有快照缓冲区。
 * 说明：
 *   ISR 共享缓冲区只保存“最近一帧”，任务层先复制到私有缓冲区再发送，
 *   避免回显过程中下一帧中断改写共享内容。
 */
static uint8_t g_rs485_task_buffer[RS485_APP_BUFFER_SIZE] = {0U};

/*
 * 函数作用：
 *   将 USART1 ISR 共享缓冲区中的一整帧数据原子地转存到任务层私有缓冲区。
 * 主要流程：
 *   1. 关总中断，避免复制过程中 USART1 ISR 再次改写共享缓冲区。
 *   2. 读取共享长度并按任务层缓冲区大小做最终边界裁剪。
 *   3. 复制完成后清除共享长度和完成标志，恢复中断。
 * 参数说明：
 *   task_buffer：任务层私有缓冲区起始地址，必须非空。
 *   task_buf_size：任务层私有缓冲区大小，单位为字节。
 * 返回值说明：
 *   大于 0：表示成功取出一帧，返回该帧有效字节数。
 *   0：表示当前没有新帧或参数无效。
 */
static uint16_t prv_rs485_take_frame(uint8_t *task_buffer, uint16_t task_buf_size)
{
    uint16_t valid_length = 0U;

    if((NULL == task_buffer) || (0U == task_buf_size)){
        return 0U;
    }

    __disable_irq();
    if(0U != rs485_rx_flag){
        valid_length = rs485_dma_length;
        if(valid_length > task_buf_size){
            valid_length = task_buf_size;
        }
        if(valid_length > 0U){
            memcpy(task_buffer, rs485_dma_buffer, valid_length);
        }
        rs485_dma_length = 0U;
        rs485_rx_flag = 0U;
    }
    __enable_irq();

    return valid_length;
}

/*
 * 函数作用：
 *   周期性处理 USART1/RS485 上收到的一整帧数据，并通过 USART1 原样回显。
 * 主要流程：
 *   1. 从 ISR 共享缓冲区原子取出一整帧到任务层私有缓冲区。
 *   2. 若本次确实收到有效数据，则先切到发送态。
 *   3. 通过 USART1 原样发送这一帧，发送完成后立即切回接收态。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   无返回值。
 */
void rs485_task(void)
{
    uint16_t rx_length;

    rx_length = prv_rs485_take_frame(g_rs485_task_buffer, (uint16_t)sizeof(g_rs485_task_buffer));
    if(0U == rx_length){
        return;
    }

    /*
     * RS485 为半双工收发器，回显前必须先切到发送态；
     * 发送完成后再切回接收态，避免挡住下一帧外部输入。
     */
    bsp_rs485_direction_transmit();
    (void)bsp_usart_send_buffer(USART1, g_rs485_task_buffer, rx_length);
    bsp_rs485_direction_receive();
}
