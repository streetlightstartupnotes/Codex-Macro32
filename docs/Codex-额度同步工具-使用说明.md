# Codex 额度同步工具使用说明

> 当前仅适用于 macOS。工具依赖 macOS CoreBluetooth 与本机 Codex，Windows/Linux 不支持。

## 用途

工具从本机 Codex app-server 的结构化额度接口读取周额度，再经已配对的 BLE Companion 特征发送给 Codex Micro。它不读取或保存账号密码、Token 或 API Key。

## 使用

1. 先在 macOS 蓝牙设置中完成 `Codex Micro` 配对，并确认设备已连接。
2. 双击 `tools/Codex-Quota-Sync/一键额度同步.command`。
3. 首次运行会在工具目录创建 `.venv` 并安装 Bleak，请等待完成。
4. 保持终端窗口开启；默认每 60 秒更新。按 `Control+C` 停止。

若 macOS 阻止脚本，右键 `.command` 文件并选择“打开”。工具只取回系统已连接的外设，不主动扫描或发起配对；未连接时只等待，也不读取额度。

## 常见问题

- 设备显示“等待连接”：先完成蓝牙连接，USB 连接不会替代 BLE。
- 已连接但显示“未同步额度”：确认 Codex 已登录、同步终端无错误，并等待下一次更新。
- 安装依赖失败：确认 `python3 --version` 可运行且网络可访问 Python 包源。
- 多个同步程序争用：不要同时运行独立工具、2.0 GUI 和其他 bridge 实例。
