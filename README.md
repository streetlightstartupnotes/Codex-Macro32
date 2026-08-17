# Codex Macro32

Codex Macro32 把 Waveshare ESP32-S3-Touch-LCD-1.85B 圆屏开发板变成一台 Codex 桌面控制器。设备通过蓝牙控制 Codex、显示六个 Agent 的状态，并由 macOS Companion 同步周额度和任务信息。V3 插线后还能作为 Mac 的板载麦克风输入。

> 当前仅适用于 macOS。Windows 与 Linux 尚未适配和测试。项目依赖 ChatGPT Desktop/Codex 当前提供的兼容行为，应用升级后可能需要同步调整。

## 当前版本

推荐使用 [Codex Macro32 V3.0.4](docs/Codex-Micro-3.0-使用说明.md)。这一版已经包含

- 六个 Agent 圆形按钮，状态颜色填充整颗按钮
- 中央额度环、中文重置时间和环内电量显示
- 绿色、黄色、红色三档额度颜色
- 六快捷页和四向摇杆页
- 独占录音波形页
- 任务完成、等待处理、报错三种提示音，无普通按键音
- 蓝牙重新配对和恢复出厂
- `Codex Macro32 Mic` 48 kHz、16 位、单声道 USB 麦克风
- USB 运行时仅提供麦克风，Codex 控制、状态和额度全部走蓝牙
- Codex Micro 设置中的亮度仅同步屏幕背光，不重复缩放或清除 Agent 状态颜色，并支持 Codex 的自动调暗时间

V2 保持在已验证的稳定状态，V3 的 UI、USB 麦克风和连接逻辑不会回写到 V2。

## 仓库目录

| 目录 | 内容 | 文档 |
| --- | --- | --- |
| `firmware/Codex-Micro-1.0` | 第一代稳定固件源码 | [1.0 使用说明](docs/Codex-Micro-1.0-使用说明.md) |
| `firmware/Codex-Micro-2.0` | 第二版稳定功能版 | [2.0 使用说明](docs/Codex-Micro-2.0-使用说明.md) |
| `firmware/Codex-Micro-3.0` | 第三版固件、Companion 和构建工具 | [3.0 使用说明](docs/Codex-Micro-3.0-使用说明.md) |
| `tools/Codex-Quota-Sync` | 独立的 macOS 额度同步工具 | [同步工具说明](docs/Codex-额度同步工具-使用说明.md) |

公开仓库只放源码和说明，不放预编译 BIN、虚拟环境、构建缓存或本机配置。

## 快速开始

准备一块 Waveshare ESP32-S3-Touch-LCD-1.85B、一根支持数据传输的 USB 线、Python 3 和 PlatformIO。

```sh
git clone https://github.com/streetlightstartupnotes/Codex-Macro32.git
cd Codex-Macro32/firmware/Codex-Micro-3.0
python3 -m pip install platformio
./flash.command
```

`flash.command` 会先检查代码，再构建 V3 USB 麦克风版。普通升级只写 `0x10000` 应用区，保留 NVS、蓝牙绑定、额度缓存和 Companion 设置。

稳定版不暴露运行时维护串口，刷机前需要先让开发板进入 ROM 下载模式。这款板只有 BOOT 和 PWR 两颗实体键：先拔掉 USB，再短按 PWR 确认关机；按住 BOOT 后插入 USB，必要时再短按 PWR 上电，等待约两秒后松开 BOOT，然后运行 `./flash.command`。USB 插着时会持续供电，PWR 不能让整机保持真正断电。从早期带维护串口的 V3 升级时，脚本仍兼容一次旧版 1200 波特触发。

刷入后继续完成以下设置。

1. 在 macOS 蓝牙设置中配对 `Codex Micro`。
2. 重新打开 ChatGPT Desktop，并为 ChatGPT/Codex 开启输入监控权限。
3. 双击 `firmware/Codex-Micro-3.0/companion/launch_companion.command`。
4. 等待 Companion 写入额度和任务数据。

Companion 默认会在检测到 `Codex Macro32 Mic` 插入后把它设为系统默认输入，拔线时恢复此前的麦克风。GUI 中可以关闭这个选项。GUI、命令行和登录自启共用进程锁，同一时间只会有一个实例控制蓝牙和默认输入。

## 操作速查

| 操作 | 结果 |
| --- | --- |
| 点击 `1` 到 `6` | 切换对应 Agent；界面 1–6 对应 Codex 槽位 `AG00`–`AG05` |
| 长按 `1` 到 `6` | 临时显示任务详情 |
| 中央短点 | 发送当前输入 |
| 中央按住约 0.8 秒后松手 | 进入六快捷页 |
| 中央按住约 5 秒后松手 | 进入四向摇杆页 |
| 中央按住约 10 秒后松手 | 清除旧蓝牙绑定并重新广播 |
| 中央持续按住 20 秒 | 清除 NVS、绑定、额度和设置 |
| 快捷页或摇杆页点击中央 | 返回首页 |
| 按住和松开 BOOT | 开始和结束 Push-to-talk |
| 350 ms 内双击 BOOT | 锁定录音，再按一次停止 |

完整操作、状态文字、USB 麦克风和排错方法见 [V3 使用说明](docs/Codex-Micro-3.0-使用说明.md)。构建环境、协议和回归清单见 [V3 固件 README](firmware/Codex-Micro-3.0/README.md)。

## 许可与第三方代码

本仓库采用分层许可。上游 MIT、Apache-2.0 等内容继续遵循各自原许可，项目新增内容的使用边界见 [有限开放源码使用说明](LIMITED_SOURCE_USE.md) 和 [第三方声明](THIRD_PARTY_NOTICES.md)。

## 作者与维护

项目发布与维护为路灯同学创业笔记。

- [X / Twitter](https://x.com/LDstartupnotes)
- [小红书](https://www.xiaohongshu.com/user/profile/63fd97c1000000001400d0ea)
- [GitHub](https://github.com/streetlightstartupnotes)

本项目与 OpenAI、Codex、ChatGPT、Waveshare 或 Work Louder 无官方隶属或背书关系。产品和商标归各自权利人所有。
