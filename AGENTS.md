<!-- TRELLIS:START -->
# Trellis Instructions

These instructions are for AI assistants working in this project.

Use the `/trellis:start` command when starting a new session to:
- Initialize your developer identity
- Understand current project context
- Read relevant guidelines

Use `@/.trellis/` to learn:
- Development workflow (`workflow.md`)
- Project structure guidelines (`spec/`)
- Developer workspace (`workspace/`)

If you're using Codex, project-scoped helpers may also live in:
- `.agents/skills/` for reusable Trellis skills
- `.codex/agents/` for optional custom subagents

Keep this managed block so 'trellis update' can refresh the instructions.

<!-- TRELLIS:END -->

# 项目自定义提示词

## 回答与代码语言

- 所有答复必须使用中文
- 代码中的注释必须使用中文

## 文档同步规范

- 只要修改了代码、配置、工程参数、协议、默认值或操作流程，必须同步检查并更新对应文档，不能只改实现不改说明
- 需要同步检查的内容包括但不限于：引脚、地址、波特率、分区、命令、编译步骤、烧录步骤、升级步骤、日志输出、资源占用、限制条件
- 如果本次改动会影响用户操作方式，必须把新的操作步骤、示例命令、预期输出和验证方法写入仓库文档
- 如果本次改动涉及跨文件约定、架构约束或长期维护规则，还必须同步更新 `.trellis/spec/` 中对应说明
- 提交前必须确认代码、仓库文档、`AGENTS.md` 提示词和 `.trellis/spec/` 描述一致，不能保留旧参数、旧命令或过期示例

## 函数注释规范

- 新写函数、重构函数、以及修改过核心逻辑的函数，函数定义前都必须补充完整注释
- 函数前注释必须明确写出“概况/作用”，说明这个函数是干什么的
- 函数前注释必须逐个列出参数含义，说明每个参数代表什么、用途是什么、取值有什么约束
- 如果函数有返回值，必须单独说明返回值含义，包括成功、失败、边界情况分别表示什么
- 如果函数无返回值，必须明确写出“无返回值”
- 不允许只写空泛注释，例如“初始化函数”“发送数据”等；必须写清楚主要行为和关键影响

推荐注释模板如下：

```c
/*
 * 函数作用：
 *   概括说明这个函数是做什么的、解决什么问题。
 * 参数说明：
 *   param1：说明参数含义、数据来源、单位或取值范围。
 *   param2：说明参数在本函数中的用途，以及特殊约束。
 * 返回值说明：
 *   0：表示成功。
 *   非 0：表示失败，具体错误含义见实现或枚举定义。
 */
```

无返回值函数使用如下模板：

```c
/*
 * 函数作用：
 *   概括说明这个函数是做什么的、解决什么问题。
 * 参数说明：
 *   param1：说明参数含义、数据来源、单位或取值范围。
 *   param2：说明参数在本函数中的用途，以及特殊约束。
 * 返回值说明：
 *   无返回值。
 */
```

## 函数内注释规范

- 函数体内部必须补充必要注释，帮助理解关键流程，而不是只在函数头写总述
- 对以下内容必须优先加注释：
  - 状态切换
  - 边界条件处理
  - 超时与错误分支
  - 中断、并发、锁、临界区
  - DMA、寄存器、GPIO、电平切换、时序要求
  - 资源申请、释放、复位、重装载
- 注释要解释“为什么这么做”，而不只是重复代码表面动作
- 明显一眼能看懂的赋值、简单判断、直接返回，不要堆砌无意义注释

推荐风格示例如下：

```c
/* 先切到发送态，再写串口数据，避免 485 收发器仍停留在接收模式。 */
bsp_rs485_direction_transmit();

/* 这里只短暂关闭中断，确保复制共享缓冲区时不会被 ISR 改写。 */
__disable_irq();
```
