# Codex Micro Companion 1.1

Companion 是 macOS 上的轻量 BLE bridge。它把 Codex 周额度、最近任务元数据和声音
设置写入现有的加密 companion GATT characteristic；不读取或保存 Codex 账号密码、
Token 或 API Key。

## 图形界面

双击 `launch_companion.command`，或在终端运行：

```sh
cd companion
./launch_companion.command
```

脚本会在需要时创建 `.venv` 并安装 `requirements.txt`。GUI 包含：

- 当前连接状态；
- 周额度显示；
- 同步间隔，建议使用 300–600 秒（5–10 分钟），默认 300 秒；
- 完成提示音开关；
- 最近六个 Agent/任务的标题、工作区、状态、创建时间和更新时间预览；
- 完整工作目录查看和“立即同步”。

设置保存在：

```text
~/Library/Application Support/Codex Micro Companion/settings.json
```

V1.1 固件会接收同步间隔与声音设置。设备界面固定使用青蓝贾维斯配色，
不提供主题选择或隐藏换色手势；声音开关控制设备提示音。
设备翻面静音是独立保护条件：声音开关开启但设备仍朝下时，依然保持静音。

## 命令行运行

从项目根目录运行自动准备环境的脚本：

```sh
./run-usage-bridge.command
```

或手动安装和启动：

```sh
cd companion
python3 -m venv .venv
.venv/bin/pip install -r requirements.txt
.venv/bin/python codex_usage_bridge.py --interval 300
```

可用参数：

```text
--interval 600             每 10 分钟同步
--silent                  关闭完成提示音
--device "Codex Micro"    指定设备名
--codex /path/to/codex    指定 Codex 可执行文件
```

GUI、CLI bridge 和 `install-usage-bridge.command` 安装的登录自启动实例不要同时运行，
否则多个进程可能争用同一 BLE GATT 连接。登录自启动使用 CLI 默认设置，不读取 GUI
设置文件。

## 连接与空闲行为

Bridge 只在 macOS 上使用 CoreBluetooth 的
`retrieveConnectedPeripheralsWithServices` 取回系统已经连接的 `Codex Micro`，不调用
`BleakScanner` 或其他主动扫描 API。

执行顺序固定为：

1. 查询系统已有连接；
2. 没有连接时等待，不启动 Codex app-server，也不读取额度或任务；
3. 建立 GATT client 后才启动独立 app-server；
4. 同步完成后按设置间隔等待；
5. 断连后关闭 app-server，回到无扫描等待状态。

断连时点击“立即同步”只会排队，待设备重新出现在系统已有连接中后执行。Bridge
不会为了同步而主动配对、重新开启设备广播或唤醒未连接设备。

## 数据来源与真实状态降级

- 优先使用 `account/rateLimits/read` 提供的额度窗口；如果独立 app-server 仅以 API Key
  登录、无权读取 ChatGPT 套餐额度，则读取最近 Codex 任务日志中 `token_count` 事件携带的
  官方额度快照。只解析结构化 `rate_limits` 字段，不读取对话正文，也不猜测额度。
- bridge 选择持续时间最长的可用窗口作为周额度。
- `thread/list` 使用 `updated_at desc` 获取最多六项，读取显式任务标题、首条消息摘要、
  `cwd`、`createdAt`、`updatedAt` 和协议返回的 `status`。
- 无显式标题时使用首条消息摘要，并在 GUI 中以 `*` 标明来源。
- 工作区名称来自 `cwd` 的最后一级，GUI 仍可显示完整路径。

Companion 启动的是独立 app-server，通常无法观察 ChatGPT Desktop 进程中已经加载的
任务。此时 `thread/list` 返回 `notLoaded`。Bridge 会在电脑界面显示“不可用”，并编码为 `u`，
不会依据更新时间推断为运行、完成或空闲。标题、目录和时间仍可同步；设备通过
Vendor HID 收到的灯光状态是另一条通路，不受该元数据降级影响。

已识别的状态映射如下：

| app-server 状态 | GUI | BLE 状态码 |
| --- | --- | --- |
| `active` | 运行中 | `r` |
| `active` 且等待审批/用户输入 | 等待中 | `w` |
| `idle` | 已空闲 | `i` |
| `systemError` | 异常 | `e` |
| `notLoaded` 或未知值 | 不可用 | `u` |

Codex app-server 协议可能随应用版本变化。方法或字段不可用时，GUI 会显示错误；bridge
不会生成虚构的任务数据。

## V1 与 V1.1 兼容性

- V1 固件只识别 `weekly_left` 和 `reset_seconds`，仍可更新额度；
  它会忽略 V1.1 设置和 Agent 元数据包。
- V1.1 固件继续接受 V1 额度字段，并额外接收设置、同步时间和最多六项 Agent 元数据。

## BLE JSON

每次同步先写一个兼容 V1 的额度/设置首包，再按 Agent 单独写包。所有 JSON 使用
UTF-8 和紧凑分隔符；单包最多 180 字节，以适配 macOS 常见的 185-byte ATT MTU。
超长标题、工作区和路径会按 UTF-8 边界裁剪，GUI 本地预览仍保留完整数据。

```json
{"v":11,"ts":1785989207,"c":[1,300,0,1],"n":1,"weekly_left":80,"reset_seconds":3600}
{"v":11,"a":[0,"任务标题","工作区","/工作/目录","u",1785986579,1785989207]}
```

- `v`：companion 协议版本，V1.1 使用 `11`；
- `ts`：同步时 Unix 时间，用于设备侧运行时长显示；
- `c`：兼容位（固定 1）、同步秒数、保留位（固定 0）、声音开关；保留位不会改变配色；
- `n`：本次 Agent 数量；
- `a`：槽位、标题、工作区、cwd、状态码、创建时间、更新时间。

旧版额度字段保留原名，便于 V1 固件继续工作。Characteristic 在固件中要求加密读写；
需要先由 macOS 完成设备配对。

## 常见问题

- GUI 一直显示等待：先在 macOS 蓝牙设置完成配对，并让 ChatGPT Desktop 建立连接；
  bridge 不会自行扫描或配对。
- 所有任务状态均为“不可用”：这是独立 app-server 返回 `notLoaded` 时的预期降级，
  不代表任务已停止。
- 标题和额度为空：查看 GUI 底部错误或 CLI 标准错误输出，确认当前 Codex 可执行文件
  支持对应方法且账号已登录。
- 设置未到设备：确认刷入的是从当前源码构建的 V1.1 固件；V1 稳定整包只消费额度字段。
- macOS 阻止 `.command`：右键脚本并选择“打开”。
