# Codex Micro for Waveshare ESP32-S3-Touch-LCD-1.85B

> 当前公开版本仅适用于 macOS。项目发布与维护：路灯同学创业笔记（https://github.com/streetlightstartupnotes）。

把微雪 ESP32-S3-Touch-LCD-1.85B 变成可被 ChatGPT Desktop 识别的便携
Codex Micro 控制器。项目使用 BLE Vendor HID 兼容协议，不是普通键盘快捷键模拟。

## 已实现

- 360×360 圆屏六 Agent 环形界面和任务状态颜色
- 点击 A1-A6 切换对应 Codex 任务
- 中央显示 Codex 周额度、剩余百分比和重置倒计时
- 中央点击发送输入框内容
- 未连接时双击中央圆环重新开启蓝牙广播；已连接时不会重复配对
- 中央长按切换六 Command 键：Fast、Approve、Decline、Fork、Mic、Send
- 屏幕滑动模拟 Codex Micro 摇杆四方向
- BOOT 键按住/松开触发 Push-to-talk
- Agent 从运行蓝色转为完成绿色时播放双音提醒
- BQ27220 电量读取和 BLE 电量上报
- 断线自动重新广播

## 硬件限制

1.85B 的 PWR 键直接连接电源锁存芯片，不进入 ESP32 GPIO，不能作为第二个应用
按键。可编程物理键只有 BOOT(GPIO0)。板载麦克风没有通过本固件上传音频；PTT
使用电脑麦克风，与官方 Codex Micro 一致。提示音需要外壳内已连接扬声器。

## 编译和烧录

最省事的方法：双击 `flash.command`。脚本从源码构建、自动选择唯一串口并上传；如果
检测到多个串口，可在终端执行 `./flash.command /dev/cu.your-device`。连接阶段若停住，
按住 BOOT，出现写入进度后松开。

需要 Python 3 和 PlatformIO：

```sh
python3 -m pip install platformio
pio run -e waveshare-1_85b
pio run -e waveshare-1_85b -t upload --upload-port "$PORT"
pio device monitor -p "$PORT" -b 115200
```

首次烧录完成后，在 macOS 蓝牙设置中配对名为 `Codex Micro` 的设备，重启
ChatGPT Desktop，并允许“输入监控”。随后打开 ChatGPT 的
`Settings > Codex Micro` 配置 Agent、Command 和方向动作。

如果旧描述符被系统缓存，先在蓝牙设置中忽略 `Codex Micro`，重启设备后重新配对。

## 周额度同步

额度不属于 Codex Micro HID 灯光协议。本项目额外提供只读 companion bridge，从
本机 Codex app-server 的 `account/rateLimits/read` 读取当前账户额度，再经自定义
BLE GATT 特征发送到屏幕。它不读取或保存账号密码、Token 或 API Key。

```sh
cd companion
python3 -m venv .venv
.venv/bin/pip install -r requirements.txt
.venv/bin/python codex_usage_bridge.py
```

Bridge 默认每 60 秒更新一次。固件脱离电脑 USB 后仍可通过 BLE 更新额度；电脑端
需要保持 bridge 进程运行。

macOS 可以直接双击 `一键额度同步.command`。首次运行会自动建立 Python 环境，
之后程序只检查已经存在的 `Codex Micro` 蓝牙连接：未连接时等待且不主动扫描，
连接后读取并更新额度。运行期间需要保持终端窗口开启。

## 操作

| 操作 | 行为 |
| --- | --- |
| 点击 A1-A6 | 切换对应 Agent |
| 点击中央圆环 | Send |
| 未连接时双击中央圆环 | 重新开启蓝牙广播并等待已绑定电脑连接 |
| 长按中央圆环 | Agent / Command 页面切换 |
| 持续按住中央圆环 20 秒 | 清除蓝牙绑定、额度缓存和设置，恢复出厂并重新配对 |
| 上下左右滑动 | 模拟摇杆四方向，可在 ChatGPT 中重映射 |
| 按住 BOOT | Push-to-talk |
| 松开 BOOT | 结束语音输入 |

恢复出厂不需要电脑端开发工具。长按中央圆环时，3 秒后屏幕开始显示 20 秒
倒计时；保持到 0 后设备会清空 NVS、全部蓝牙绑定和额度缓存，并自动重启广播。

## 兼容性和风险

该固件依赖未公开的 Codex Micro Vendor HID 协议。ChatGPT Desktop 更新后可能需要
同步调整。当前仅支持 macOS；Windows 与 Linux 尚未适配和测试。项目不模拟 Work Louder
Input 的额外层、固件升级器或原厂 RGB 灯效。
