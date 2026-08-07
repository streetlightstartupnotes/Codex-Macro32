# Codex Micro 1.0 使用说明

> 当前仅支持并测试 macOS；Windows 与 Linux 不在支持范围。

1.0 是冻结的第一代稳定源码，适用于 Waveshare ESP32-S3-Touch-LCD-1.85B。公开仓库不附带预编译 BIN，请在 Mac 上本地构建。

## 安装与刷写

1. 安装 Python 3 与 PlatformIO：`python3 -m pip install platformio`。
2. 用数据线连接设备，进入 `firmware/Codex-Micro-1.0`。
3. 双击 `flash.command`；也可执行 `./flash.command /dev/cu.usbmodemNNN`。
4. 如果连接停住，按住 BOOT，看到写入进度后松开。
5. 在 macOS 蓝牙设置中配对 `Codex Micro`，重启 ChatGPT Desktop，并允许“输入监控”。

## 操作

| 操作 | 结果 |
| --- | --- |
| 点击 A1–A6 | 切换对应 Agent |
| 点击中央 | 发送当前输入 |
| 未连接时双击中央 | 重新开启蓝牙广播 |
| 长按中央 | 切换 Agent/Command 页面 |
| 上下左右滑动 | 发送四方向控制，可在 Codex 中映射 |
| 按住/松开 BOOT | 开始/结束语音输入 |
| 持续按住中央 20 秒 | 清除 NVS、绑定和额度缓存并重新配对 |

## 额度同步

双击本目录的 `一键额度同步.command`，首次运行会创建本地 Python 环境。设备必须先由 macOS 完成蓝牙配对；终端窗口需保持开启。该工具不保存密码、Token 或 API Key。

1.0 是历史稳定版；新功能、当前 UI、分级长按、动态录音波形和 Companion 1.1 请使用 2.0。
