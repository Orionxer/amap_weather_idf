# ESP-IDF Agent Rules

## 核心约束
- **Tmux**：优先复用已有会话（会话名取当前目录名）；仅在必要时新建。所有 `idf.py` 命令必须在 tmux 内执行，禁止在普通 shell 执行。
- **环境**：执行 IDF 命令前需确保 `source ~/.espressif/tools/activate_idf_v5.5.3.sh` 已执行。
- **禁止替代**：禁止使用 `python .../idf.py`、`$IDF_PYTHON_ENV_PATH/bin/python .../idf.py` 等方式替代 `idf.py`。
- **失败处理**：若 `idf.py` 不可用，先修复环境（重新激活/检查 PATH），未修复前不得构建，并向用户报告。
- **串口互斥**：若 `idf.py monitor` 运行中，先 `Ctrl+]` 退出再执行其他命令。
- **禁止命令**：严禁 `idf.py fullclean`。
- **LSP**：不要运行 LSP 诊断/语言服务器验证，除非用户明确请求。
- **主动执行**：除非用户明确要求，否则不要主动编译、烧录、监控。
- **目标确认**：执行任何 `idf.py build/flash/size` 前，必须先按 **目标设备** 流程确认芯片型号，确保 `idf.py set-target` 已执行且正确。

## 目标设备 (首次执行任务前检查)

### 自动检测
优先从 `sdkconfig` 读取 `CONFIG_IDF_TARGET`。若读取到目标，该目标即视为当前工程目标芯片，不再执行 `idf.py set-target`，除非用户要求选择芯片或者读取失败，进入交互选择。

### 交互选择
首次编译、`sdkconfig` 读取失败，或用户明确要求弹窗确认目标设备时，使用 AskUserQuestion 弹窗让用户选择芯片型号（不允许多选），可选：esp32 / esp32p4 / esp32s3 / esp32c5 等，或 Other 手动输入。
确定型号后执行 `idf.py set-target <chip>`。

## 事件通道
tmux 内命令末尾拼接 `; tmux wait-for -S <channel>` 发信号，AI 侧用 `tmux wait-for <channel>` 阻塞等待。

| channel | 触发场景 | 成功标记 | 失败标记 |
|:---|:---|:---|:---|
| `build_event` | 编译 | `===BUILD_DONE===` | `===BUILD_FAILED===` |
| `flash_event` | 烧录 | `===FLASH_DONE===` | `===FLASH_FAILED===` |
| `size_event` | 固件大小分析 | `===SIZE_DONE===` | — |

信号到达后，`tmux capture-pane -t <session> -p` 读取输出，检查标记判断成败。

## 任务模式
前置：进入 tmux → 确保环境已激活。严禁用 `&&` 串联激活和 IDF 命令。

### Build
```
idf.py build && echo "===BUILD_DONE===" || echo "===BUILD_FAILED==="; tmux wait-for -S build_event
```
AI 等待：`tmux wait-for build_event`，读取结果检查标记。

### Flash
```
idf.py -b 6000000 flash && echo "===FLASH_DONE===" || echo "===FLASH_FAILED==="; tmux wait-for -S flash_event
```
AI 等待：`tmux wait-for flash_event`，若失败按 **硬件错误排查** 执行。

### Size
```
idf.py size && echo "===SIZE_DONE==="; tmux wait-for -S size_event
```
AI 等待：`tmux wait-for size_event`，仅汇报表格（单位自适应 KB/MB）：
| 项目 | 总大小 | 当前大小 | 使用率 | 剩余大小 |
| :--- | :--- | :--- | :--- | :--- |
| 固件分区 | - | - | - | - |
| HP SRAM | - | - | - | - |

### Monitor
```
idf.py monitor
```
无事件信号，按需 `tmux capture-pane -p` 获取输出。若启动失败按 **硬件错误排查** 执行。

### Build + Flash + Monitor
按 Build → 成功→ Flash → 成功→ Monitor 顺序执行（与上述单步相同）。任一步 `*_FAILED` 即中断，按 **硬件错误排查** 执行。

## 硬件错误排查
Flash/Monitor 失败时：
1. 诊断：检查 [未挂载] / [无权限] / [端口占用]。
2. 指引（提示用户替换 `BUSID`/`PORT`）：
   - 未挂载：`usbipd.exe attach --wsl --busid <BUSID>`
   - 无权限：`sudo chmod 666 <PORT>`
   - 端口占用：`lsof <PORT>`
3. 优先自动获取端口（`ls /dev/tty*`），失败再提示手动指定。
