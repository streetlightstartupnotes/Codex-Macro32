# Codex Micro 2.0 使用说明

> 当前仅支持并测试 macOS；Windows 与 Linux 不在支持范围。

2.0 是当前推荐版，适用于 Waveshare ESP32-S3-Touch-LCD-1.85B。它包含六 Agent 状态页、0.8/5/10/20 秒中央长按分级、六快捷操作、四向摇杆、独占录音波形、任务状态提示音、BLE Companion 和额度缓存。自动旋转与 USB Vendor HID 均已关闭。

## 安装

1. 安装 Python 3 与 PlatformIO：`python3 -m pip install platformio`。
2. 连接设备，进入 `firmware/Codex-Micro-2.0`。
3. 先运行 `./scripts/check-regressions.sh`，再双击 `flash.command`；也可执行 `./flash.command /dev/cu.usbmodemNNN`。
4. 如果连接停住，按住 BOOT，看到写入进度后松开。
5. 在 macOS 蓝牙设置中配对 `Codex Micro`，重启 ChatGPT Desktop，并允许“输入监控”。
6. 双击 `companion/launch_companion.command`，连接后同步额度、任务摘要与声音设置。

## 首页与状态

- 点击 1–6 切换对应 Agent；界面编号 1–6 对应 Codex 槽位 `AG00`–`AG05`，长按某个编号显示详情。
- 状态颜色填满整个按钮：蓝色运行、绿色完成、黄色等待、红色错误、灰蓝色空闲/未分配。
- 蓝牙未连接且无缓存时显示“等待连接”；已连接但尚无额度时显示“未同步额度”；有缓存时保留最近额度。
- 动态文字含字库不支持字符时整行隐藏，不显示方框或问号。

## 中央按住分级

| 持续时间 | 松手后的行为 |
| ---: | --- |
| 约 0.8 秒 | 进入六快捷页 |
| 约 5 秒 | 进入四向摇杆页 |
| 约 10 秒 | 六个球消失并显示“重新连接”，松手清除蓝牙绑定后重新广播 |
| 持续到 20 秒 | 清除 NVS、绑定、额度与设置，重启后自动广播 |

快捷页和摇杆页都不会因松手退出；进入后点击中央一次才返回首页。

## 快捷操作与摇杆

- 六快捷键：快速、同意、拒绝、分支、录音、发送。
- 摇杆上：新任务；下：侧边栏；右：旋钮前进；左：旋钮后退。左右的实际行为跟随 Codex 中选择的旋钮模式。

## 录音与声音

- 快捷页点击“录音”后进入独占波形页，波形持续动态并随声音强弱变化；按一次 BOOT 停止。
- 按住/松开 BOOT 可进行普通 Push-to-talk；350ms 内双击 BOOT 可锁定录音，再按一次停止。
- 没有任何按键音。只有任务完成、等待处理和报错播放与绿色、黄色、红色对应的提示音。
- 屏幕朝下会静音，翻回朝上恢复；自动旋转不会启动。

## 屏幕、电源与 USB

- 设备通电时保持常亮；短按硬件 PWR 键关机。
- USB 仅用于供电、刷机和串口日志，不承担 Codex 控制或额度同步。
- 板载麦克风用于波形能量显示；实际 Codex 语音仍使用 Mac 麦克风。

更完整的协议、故障排查、构建和恢复说明见 [`firmware/Codex-Micro-2.0/docs/OPERATIONS.md`](../firmware/Codex-Micro-2.0/docs/OPERATIONS.md)。
