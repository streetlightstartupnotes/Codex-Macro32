# Codex Macro32

把 Waveshare ESP32-S3-Touch-LCD-1.85B 圆屏开发板变成 Codex 桌面控制器，并通过 macOS Companion 同步额度和任务信息。

> **当前仅适用于 macOS。** Windows 与 Linux 尚未适配、测试或提供支持。项目依赖 ChatGPT Desktop/Codex 当前的行为和非公开 Vendor HID 兼容协议，应用升级后可能需要同步调整。

## 项目内容

| 目录 | 内容 | 使用说明 |
| --- | --- | --- |
| `firmware/Codex-Micro-1.0` | 第一代稳定固件源码 | [1.0 使用说明](docs/Codex-Micro-1.0-使用说明.md) |
| `firmware/Codex-Micro-2.0` | 当前功能版：六 Agent、快捷操作、摇杆、录音波形、任务提示音与 Companion 1.1 | [2.0 使用说明](docs/Codex-Micro-2.0-使用说明.md) |
| `tools/Codex-Quota-Sync` | 独立的 macOS 额度同步工具 | [额度同步工具说明](docs/Codex-额度同步工具-使用说明.md) |

仓库只发布经过检查的源码，不包含原始 ZIP、预编译 BIN、本机虚拟环境或构建缓存。请在自己的 Mac 上构建，避免把构建机路径固化进公开二进制。

## 快速开始

1. 准备 Waveshare ESP32-S3-Touch-LCD-1.85B、数据线、Python 3 和 PlatformIO。
2. 优先进入 `firmware/Codex-Micro-2.0`，双击 `flash.command` 或按对应说明从终端构建并刷入。
3. 在 macOS 蓝牙设置中配对 `Codex Micro`，为 ChatGPT/Codex 开启“输入监控”。
4. 双击 `firmware/Codex-Micro-2.0/companion/launch_companion.command`，连接后同步额度和任务信息。

详细按键、长按分级、录音、摇杆、提示音、重新配对和恢复出厂操作见 [2.0 完整说明](docs/Codex-Micro-2.0-使用说明.md)。

## 安全与隐私

- Companion 不要求保存 Codex 密码、Token 或 API Key。
- Wi‑Fi OTA 默认关闭；如需启用，只在本地创建 `CodexV11Secrets.h`，该文件已被忽略，禁止提交。
- USB Vendor HID 已从 2.0 移除；USB 仅用于供电、刷写和串口，Codex 控制与 Companion 数据走 BLE。
- 发布前检查结果和报告见 [SECURITY.md](SECURITY.md)。

## 有限开放源码与第三方许可

这是“有限开放源码（source-available）”发布，不应表述为所有内容均采用单一开源许可证：

- 上游 `imliubo/codex-micro-4-core2` 的 MIT 代码继续遵循 MIT License；
- Waveshare 等第三方组件继续遵循其 Apache-2.0/MIT 等原许可；
- 路灯同学创业笔记新增的品牌素材、说明文档、UI 设计表达和项目整合部分允许个人学习、研究和非商业修改，商业使用需另行取得书面许可。

完整边界见 [LIMITED_SOURCE_USE.md](LIMITED_SOURCE_USE.md)、各子目录 `LICENSE` 与 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)。有限条款不会缩减任何上游许可证已经授予的权利。

## 作者与维护

项目发布与维护：**路灯同学创业笔记**

- [X / Twitter](https://x.com/LDstartupnotes)
- [小红书](https://www.xiaohongshu.com/user/profile/63fd97c1000000001400d0ea)
- [GitHub](https://github.com/streetlightstartupnotes)

本项目与 OpenAI、Codex、ChatGPT、Waveshare 或 Work Louder 无官方隶属或背书关系。产品和商标归各自权利人所有。
