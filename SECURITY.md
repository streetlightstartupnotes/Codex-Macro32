# 安全与隐私

## 发布前检查

2026-08-07 对公开目录进行了文本凭据、私钥、本机绝对路径、缓存和构建产物检查：未发现真实 API Key、GitHub Token、私钥、Wi‑Fi SSID/密码或 Codex 登录凭据。

原始 ZIP/BIN 未发布，因为预编译固件包含构建机的本机目录路径；`.venv`、`.pio`、`__pycache__`、`.DS_Store`、`__MACOSX` 和 `CodexV11Secrets.h` 也被排除。

## 本地秘密

若启用可选 Wi‑Fi OTA，只复制 `include/CodexV11Secrets.example.h` 为 `include/CodexV11Secrets.h` 并在本机填写。不要提交或分享真实配置。OTA 只应在可信私有局域网使用。

## 报告问题

请在 GitHub 仓库提交不含秘密内容的安全问题说明。不要在 Issue 中粘贴 Token、密码、完整串口身份信息或私有目录；需要提供日志时先脱敏。
