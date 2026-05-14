/************************************************************
 * 版权：2025CIMC Copyright。
 * 文件：usart.c
 * 作者: Jialei Zhao
 * 平台: 2025CIMC IHD-V04
 * 版本: Jialei Zhao     2025/12/30     V0.01    original
************************************************************/


/************************* 头文件 *************************/

#include "usart.h"

/************************* 宏定义 *************************/


/************************ 变量定义 ************************/

/*
 * 这一组缓冲区配合 USART IDLE 中断实现“不定长帧接收”：
 * 1. recv_buf：ISR 中边收边放的临时缓冲；
 * 2. recv_real_buf：检测到一帧结束后拷贝出来的完整帧；
 * 3. recv_flag：通知主循环“有一帧可处理了”。
 *
 * 注意：当前 BootLoader 主流程并没有依赖这套接收机制去执行升级。
 * 真正的升级触发来自参数区标志位；这里更像是为后续串口命令扩展预留的基础设施。
 */
uint8_t recv_buf[128] = { 0 };
uint8_t recv_len = 0;
uint8_t recv_real_buf[128] = { 0 };
uint8_t recv_real_len = 0;
uint8_t recv_flag = 0;


/************************ 函数定义 ************************/



/************************************************************
 * Function :       usart_init
 * Comment  :       初始化 BootLoader 使用的 USART0。
 *                  主要完成：
 *                  1. 打开 USART0 中断；
 *                  2. 配置 PA9/PA10 为串口复用功能；
 *                  3. 设置默认波特率 460800；
 *                  4. 使能接收和 IDLE 中断，支持不定长帧接收。
 * Parameter:       无参数。
 * Return   :       无返回值。
 * Author   :       Jialei Zhao
 * Date     :       2025-12-30 V0.1 original
************************************************************/
void my_usart_init(void)
{
	/* 先把 NVIC 中的 USART0 中断打开，否则后续 RBNE/IDLE 中断无法进入 ISR。 */
	nvic_irq_enable(USART0_IRQn , 3 , 2);

	/* 串口外设和 GPIO 端口都要先开时钟。 */
	rcu_periph_clock_enable(USART_RCU);
	rcu_periph_clock_enable(USART_PIN_RCU);

	/* 配置 RX 引脚复用到 USART0。 */
	gpio_af_set(USART_PORT , GPIO_AF_7 , USART_RX_Pin);
	gpio_mode_set(USART_PORT , GPIO_MODE_AF , GPIO_PUPD_NONE , USART_RX_Pin);
	gpio_output_options_set(USART_PORT , GPIO_OTYPE_PP , GPIO_OSPEED_50MHZ , USART_RX_Pin);

	/* 配置 TX 引脚复用到 USART0。 */
	gpio_af_set(USART_PORT , GPIO_AF_7 , USART_TX_Pin);
	gpio_mode_set(USART_PORT , GPIO_MODE_AF , GPIO_PUPD_NONE , USART_TX_Pin);
	gpio_output_options_set(USART_PORT , GPIO_OTYPE_PP , GPIO_OSPEED_50MHZ , USART_TX_Pin);

	/* 重新初始化 USART，确保波特率和收发状态处于已知状态。 */
	usart_deinit(USART);
	usart_baudrate_set(USART , USART_DEFAULT_BAUDRATE);
	usart_receive_config(USART , USART_RECEIVE_ENABLE);
	usart_transmit_config(USART , USART_TRANSMIT_ENABLE);
	usart_enable(USART);

	/* RBNE 表示收到字节，IDLE 表示一帧不定长数据收完。 */
	usart_interrupt_enable(USART , USART_INT_RBNE);
	usart_interrupt_enable(USART , USART_INT_IDLE);

}
/************************************************************
 * Function :       usart_recv_buf
 * Comment  :       处理已经在中断里收完整的一帧串口数据。
 *                  当前实现只是把接收到的数据按字符串打印出来，主要用于调试观察。
 *                  它不参与当前两阶段升级主流程。
 * Parameter:       无参数。
 * Return   :       无返回值。
 * Author   :       Jialei Zhao
 * Date     :       2025-12-30 V0.1 original
************************************************************/
void usart_recv_buf(void)
{
	if (recv_flag)
	{
		printf("recv_real_buf: %s\n" , recv_real_buf);
		recv_flag = 0;
	}
}

/************************************************************
 * Function :       fputc
 * Comment  :       将标准库 printf 的单字符输出重定向到 USART0。
 *                  BootLoader 中几乎所有日志都依赖这个函数输出到串口终端。
 * Parameter:       ch 待发送字符。
 * Parameter:       f  标准库文件句柄，本实现未使用其内容，仅为匹配 fputc 原型。
 * Return   :       返回已经发送的字符。
 * Author   :       Jialei Zhao
 * Date     :       2026-2-4 V0.1 original
************************************************************/
int fputc(int ch , FILE* f)
{
	usart_data_transmit(USART0 , (uint8_t)ch);
	while (RESET == usart_flag_get(USART0 , USART_FLAG_TBE));
	/*
	 * 等待 TC 置位，确认最后一个字符已经从移位寄存器真正发出。
	 * BootLoader 在打印完日志后可能马上切换时钟并跳转 App；如果只等 TBE，
	 * 最后一两个字节还在发送途中就被 App 重新初始化 USART，串口终端会看到乱码。
	 */
	while (RESET == usart_flag_get(USART0 , USART_FLAG_TC));
	return ch;
}

/************************************************************
 * Function :       USART0_IRQHandler
 * Comment  :       USART0 中断服务函数。
 *                  这里采用“RBNE 收单字节 + IDLE 判一帧结束”的常见方案：
 *                  1. 只要收到字节，就先追加到 recv_buf；
 *                  2. 一旦总线空闲，说明本帧结束，把临时缓冲复制到正式缓冲；
 *                  3. 主循环后续只需要查看 recv_flag 即可。
 *
 *                  这样设计的原因，是 ISR 里只做最小量的数据搬运，不在中断中直接解析复杂协议，
 *                  便于后续扩展。
 * Parameter:       无参数。
 * Return   :       无返回值。
 * Author   :       Jialei Zhao
 * Date     :       2025-12-30 V0.1 original
************************************************************/
void USART0_IRQHandler(void)
{
	if (usart_interrupt_flag_get(USART , USART_INT_FLAG_RBNE) != RESET)
	{
		/* 每进来一个字节就先顺序写入临时缓冲。 */
		recv_buf[recv_len++] = usart_data_receive(USART);
	}

	/* IDLE 说明当前一帧已经结束，适合切换到“可处理状态”。 */
	if (usart_interrupt_flag_get(USART , USART_INT_FLAG_IDLE) != RESET && recv_len != 0)
	{
		/* 先读一次数据寄存器，配合状态寄存器访问完成 IDLE 标志清除流程。 */
		usart_data_receive(USART);

		/* 把 ISR 临时缓冲复制到正式缓冲，避免主循环处理时与 ISR 共享同一计数变量。 */
		memcpy(recv_real_buf , recv_buf , recv_len);
		recv_real_len = recv_len;
		recv_len = 0;
		recv_flag = 1;
	}
}

/****************************End*****************************/
