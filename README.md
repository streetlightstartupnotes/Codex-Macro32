# Codex Macro32

把 Waveshare ESP32-S3-Touch-LCD-1.85B 圆屏开发板变成 Codex 桌面控制器，并通过 macOS Companion 同步额度和任务信息。

> **当前仅适用于 macOS。** Windows 与 Linux 尚未适配、测试或提供支持。项目依赖 ChatGPT Desktop/Codex 当前的行为和非公开 Vendor HID 兼容协议，应用升级后可能需要同步调整。

## 项目内容

| 目录 | 内容 | 使用说明 |
| --- | --- | --- |
| `firmware/Codex-Micro-1.0` | 第一代稳定固件源码 | [1.0 使用说明](docs/Codex-Micro-1.0-使用说明.md) |
| `firmware/Codex-Micro-2.0` | 第二版稳定功能版：六 Agent、快捷操作、摇杆、录音波形与任务提示音 | [2.0 使用说明](docs/Codex-Micro-2.0-使用说明.md) |
| `firmware/Codex-Micro-3.0` | 第三版开发预览：新中央 UI、电量显示、绿黄红额度环；USB 麦克风仍在开发 | [3.0 使用说明](docs/Codex-Micro-3.0-使用说明.md) |
| `tools/Codex-Quota-Sync` | 独立的 macOS 额度同步工具 | [额度同步工具说明](docs/Codex-额度同步工具-使用说明.md) |

仓库只发布经过检查的源码，不包含原始 ZIP、预编译 BIN、本机虚拟环境或构建缓存。请在自己的 Mac 上构建，避免把构建机路径固化进公开二进制。

## 快速开始

1. 准备 Waveshare ESP32-S3-Touch-LCD-1.85B、数据线、Python 3 和 PlatformIO。
2. 稳定使用进入 `firmware/Codex-Micro-2.0`；测试第三版 UI 才进入 `firmware/Codex-Micro-3.0`。
3. 在 macOS 蓝牙设置中配对 `Codex Micro`，为 ChatGPT/Codex 开启“输入监控”。
4. 双击 `firmware/Codex-Micro-2.0/companion/launch_companion.command`，连接后同步额度和任务信息。

详细按键、长按分级、录音、摇杆、提示音、重新配对和恢复出厂操作见 [2.0 完整说明](docs/Codex-Micro-2.0-使用说明.md)。

## 作者与维护

项目发布与维护：**路灯同学创业笔记**

- [X / Twitter](https://x.com/LDstartupnotes)
- [小红书](https://www.xiaohongshu.com/user/profile/63fd97c1000000001400d0ea)
- [GitHub](https://github.com/streetlightstartupnotes)

本项目与 OpenAI、Codex、ChatGPT、Waveshare 或 Work Louder 无官方隶属或背书关系。产品和商标归各自权利人所有。
