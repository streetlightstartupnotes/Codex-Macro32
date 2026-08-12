#!/bin/sh
set -eu

PROJECT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
UI="$PROJECT_DIR/src/CodexUi.cpp"
MAIN="$PROJECT_DIR/src/main.cpp"
BLE="$PROJECT_DIR/src/CodexMicroBle.cpp"
LVGL_PORT="$PROJECT_DIR/src/LvglPort.cpp"
USB_MIC="$PROJECT_DIR/src/UsbMic.cpp"
USB_MIC_CONFIG="$PROJECT_DIR/usb-mic/platformio.ini"
USB_MIC_BOARD="$PROJECT_DIR/usb-mic/boards/waveshare-1_85b-usb-mic.json"
ES7210="$PROJECT_DIR/lib/es7210/src/es7210.cpp"
COMPANION="$PROJECT_DIR/companion/codex_usage_bridge.py"
MAC_AUDIO="$PROJECT_DIR/companion/mac_audio_input.py"
PROCESS_LOCK="$PROJECT_DIR/companion/process_lock.py"
INSTALL_BRIDGE="$PROJECT_DIR/install-usage-bridge.command"
FLASH_SCRIPT="$PROJECT_DIR/flash.command"
FLASH_PORT="$PROJECT_DIR/scripts/prepare-flash-port.py"

fail() {
  echo "回归检查失败：$1" >&2
  exit 1
}

require_text() {
  file=$1
  value=$2
  message=$3
  grep -Fq -- "$value" "$file" || fail "$message"
}

forbid_text() {
  file=$1
  value=$2
  message=$3
  if grep -Fq -- "$value" "$file"; then
    fail "$message"
  fi
}

require_text "$UI" 'constexpr int kAgentOrbit = 132;' '放大版首页六键轨道被改动'
require_text "$UI" 'constexpr int kAgentSize = 86;' '放大版首页六键尺寸被改动'
require_text "$UI" 'constexpr int kUsageSize = 176;' '放大版首页中央额度环尺寸被改动'
require_text "$UI" '&lv_font_montserrat_16' '首页 1–6 字号没有放大'
require_text "$UI" '&lv_font_montserrat_40' '中央额度数字没有使用 V3 放大字号'
require_text "$UI" 'lv_obj_set_style_text_font(usageLabel, &lv_font_montserrat_48, 0);' '长按模式预览未恢复大号数字'
require_text "$UI" 'updateBatteryVisual' '中央电量图标入口丢失'
require_text "$UI" 'quotaAccent' '额度环绿黄红状态色丢失'
require_text "$UI" 'max(2, (value * 24) / 100)' '电池填充宽度与紧凑图标不一致'
require_text "$UI" 'lv_obj_set_pos(batteryIcon, 144, 230);' '电池图标不再位于中央圆盘安全区'
require_text "$UI" 'lv_obj_align(batteryLabel, LV_ALIGN_CENTER, 22, 56);' '电量文字不再位于中央圆盘安全区'
require_text "$UI" 'lv_obj_align(resetLabel, LV_ALIGN_CENTER, 0, 32);' '重置时间未保持在中央环内'
require_text "$UI" 'LV_COLOR_MAKE(0x00, 0x00, 0x00)' '稳定版首页纯黑背景被改动'
require_text "$UI" 'snprintf(text, sizeof(text), "%d", i + 1);' '首页不再显示 1–6'
require_text "$UI" 'lv_obj_set_style_bg_color(agentButtons[i], lv_color_hex(fill), 0);' 'Agent 状态色没有填充整颗按钮'
require_text "$UI" 'safeTextForDisplay(metadata.title, "")' '动态标题不再整行安全隐藏'
require_text "$UI" 'safeTextForDisplay(metadata.workspace, "")' '动态工作区不再整行安全隐藏'
forbid_text "$UI" '"--%"' '设备 UI 又显示未知额度占位'

require_text "$UI" 'sendAgent(index);' '六个 Agent 单击入口丢失'
require_text "$UI" 'snprintf(id, sizeof(id), "AG%02d", index + 1);' 'Agent 1–6 命令编号发生偏移'
require_text "$UI" '"ACT06", "ACT07", "ACT08", "ACT09", "ACT10", "ACT12"' '第一版六个快捷命令映射丢失'
require_text "$UI" 'heldMs >= 800 && centerLongPressStage < 1' '短按 0.8s 进入六快捷键入口丢失'
require_text "$UI" 'if (centerLongPressStage == 0)' '中央短点分支丢失'
require_text "$UI" 'setCommandPage(true);' '六快捷键入口丢失'
require_text "$UI" 'heldMs >= 5000 && centerLongPressStage < 2' '长按 5s 进入摇杆入口丢失'
require_text "$UI" 'setJoystickPage(true);' '摇杆入口丢失'
require_text "$UI" 'heldMs >= 10000 && centerLongPressStage < 3' '长按 10s 重新配对入口丢失'
require_text "$UI" 'requestRebond();' '重新配对命令丢失'
require_text "$UI" 'if (commandPage)' '快捷页单击中间返回入口丢失'
require_text "$UI" 'if (joystickPage)' '摇杆页单击中间返回入口丢失'
require_text "$UI" 'ignoreNextNavCenterClick' '摇杆长按进入防误触标记丢失'
forbid_text "$UI" 'kCommandTimeoutMs' '快捷页又恢复了自动退出'
require_text "$UI" 'sendMomentary("ACT12");' '中央单击发送入口丢失'
require_text "$UI" 'code == LV_EVENT_CLICKED && !centerReleaseHandled' '中央点击事件兜底丢失'
require_text "$UI" 'if (radius <= 62.0f)' '摇杆中点退出丢失'
require_text "$UI" 'sendEncoderStep(dx > 0 ? "ENC_CC" : "ENC_CW");' '遥控左右旋钮步进映射丢失'
require_text "$UI" 'sendJoystickTap(0.25f);' '摇杆下方侧边栏映射丢失'
require_text "$UI" 'sendJoystickTap(0.75f);' '摇杆上方新任务映射丢失'
require_text "$UI" 'lv_label_set_text(usageLabel, "未同步额度");' '未同步额度文字缺失'
require_text "$UI" 'if (!state.bleConnected || !state.bleAuthenticated)' '蓝牙连接/认证状态优先级丢失'
forbid_text "$UI" 'if (state.companionReady)' '额度文案又错误依赖 Companion 已同步状态'
require_text "$UI" 'lv_label_set_text(usageLabel, "等待连接");' '未连接等待文字缺失'
require_text "$BLE" 'state_.bleAuthenticated = false;' '重新配对未立即清除蓝牙认证状态'
require_text "$BLE" 'void CodexMicroBle::onAuthenticationComplete(bool success)' '蓝牙认证完成状态回调丢失'
forbid_text "$BLE" 'usbTransport' 'USB Vendor HID 传输又被启用'
forbid_text "$UI" 'lv_label_set_text(resetLabel, "等待配对");' '等待配对使用不受支持字符'
forbid_text "$UI" 'lv_label_set_text(pageLabel, "重新配对");' '重新配对使用不受支持字符'
require_text "$UI" 'lv_label_set_text(pageLabel, "重新连接");' '重新连接文字缺失'

require_text "$UI" 'setHomeUiHidden(true);' '录音独占界面入口丢失'
require_text "$UI" 'sound->microphoneLevel()' '录音波形不再使用真实麦克风能量'
require_text "$UI" 'takeRecordingStartRequest' '快捷页录音启动请求丢失'
require_text "$MAIN" 'codex.sendKey("ACT10", 1);' '录音按下命令丢失'
require_text "$MAIN" 'codex.sendKey("ACT10", 0);' '录音松开命令丢失'
forbid_text "$MAIN" 'audio.recordingStart();' '录音开始又加入了按键音'
forbid_text "$MAIN" 'audio.recordingStop();' '录音停止又加入了按键音'
forbid_text "$UI" 'sound->completionChime();' '恢复出厂又误用了任务完成音'
require_text "$MAIN" '} else if (playApproval) {' '任务提示音优先级逻辑丢失'
require_text "$MAIN" 'setBacklight(kBacklightAwake);' '常亮策略丢失'

require_text "$USB_MIC" 'USBAudioCard audioCard(kSampleRate, UAC_BPS_16, UAC_SPK_NONE, UAC_MIC_MONO);' 'USB 不再是单声道输入设备'
require_text "$USB_MIC" 'constexpr uint32_t kSampleRate = 48000;' 'USB 麦克风采样率不再是 48 kHz'
require_text "$USB_MIC" 'audioSource->readMicrophoneSamples' 'USB 麦克风不再读取板载麦克风'
require_text "$PROJECT_DIR/src/AudioFeedback.cpp" 'es7210_begin(kSampleRate)' '板载 ES7210 双麦克风初始化丢失'
require_text "$PROJECT_DIR/src/AudioFeedback.cpp" 'I2S_RX_TRANSFORM_16_STEREO_TO_MONO' 'ES7210 双麦克风不再转换为单声道'
require_text "$ES7210" 'constexpr uint8_t kAddress = 0x40;' 'ES7210 I2C 地址发生变化'
require_text "$ES7210" '{kMainClock, 0xC1}' 'ES7210 48 kHz 时钟配置丢失'
require_text "$ES7210" '{kAdc1Volume, 0xE3}' 'ES7210 数字增益不再是实机校准的 +18 dB'
require_text "$USB_MIC" 'USB.productName("Codex Macro32 Mic");' 'USB 麦克风名称被改动'
require_text "$USB_MIC" 'static USBCDC maintenanceSerial(0);' '免按键升级维护串口丢失'
require_text "$USB_MIC" 'maintenanceSerial.enableReboot(true);' '维护串口不再允许 esptool 自动进入下载器'
forbid_text "$USB_MIC" 'USBHIDVendor' 'V3 USB 麦克风又加入 Vendor HID'
forbid_text "$USB_MIC" 'USBHIDKeyboard' 'V3 USB 麦克风又加入键盘设备'
require_text "$USB_MIC_CONFIG" 'CONFIG_BT_BLUEDROID_ENABLED=y' 'USB 麦克风构建丢失蓝牙 HID 兼容层'
require_text "$USB_MIC_CONFIG" 'default_envs = prepare-bluedroid, waveshare-1_85b-usb-mic' '首次构建不再按两阶段准备 Bluedroid 兼容库'
require_text "$USB_MIC_CONFIG" 'CONFIG_TINYUSB_AUDIO_ENABLED=1' 'USB Audio Class 未启用'
require_text "$USB_MIC_CONFIG" 'CONFIG_TINYUSB_CDC_ENABLED=1' '免按键升级维护串口未启用'
require_text "$USB_MIC_CONFIG" 'CONFIG_TINYUSB_CDC_MAX_PORTS=1' '维护串口数量配置不正确'
require_text "$USB_MIC_BOARD" '-DARDUINO_USB_CDC_ON_BOOT=0' 'USB 麦克风构建又启用 CDC'
require_text "$FLASH_SCRIPT" 'scripts/prepare-flash-port.py' '刷写脚本没有准备通用免按键端口'
require_text "$FLASH_SCRIPT" 'PROJECT_DIR="${0:A:h}"' '刷写脚本没有锁定项目根目录'
require_text "$FLASH_SCRIPT" 'system info --json-output' '刷写脚本没有查询当前 PlatformIO 的 Python'
require_text "$FLASH_SCRIPT" 'json.load(sys.stdin)["python_exe"]["value"]' '刷写脚本仍写死本机 PlatformIO Python 路径'
require_text "$FLASH_SCRIPT" '--before "$BEFORE"' '刷写脚本没有使用端口检测得到的复位方式'
require_text "$FLASH_SCRIPT" '--after watchdog-reset' '原生 USB-JTAG 刷写后可能停留在 ROM 下载器'
require_text "$FLASH_SCRIPT" 'write-flash 0x10000 "$APP_BIN"' '刷写脚本不再只写应用分区'
require_text "$FLASH_PORT" 'CODEX_MIC_PID = 0x8361' '刷写端口检测缺少 V3 维护串口产品号'
require_text "$FLASH_PORT" 'ESP32S3_ROM_PID = 0x1001' '刷写端口检测缺少 ESP32-S3 ROM 产品号'
require_text "$FLASH_PORT" 'baudrate=1200' '维护串口没有使用标准 1200 波特免按键触发'
require_text "$MAIN" 'if (codex_usb_mic::streaming()) return;' 'USB 录音时任务音效未让路'
require_text "$MAIN" 'PendingAlert pendingAlert = PendingAlert::None;' 'USB 录音期间的任务提示事件不再延后保存'
require_text "$MAIN" 'const PendingAlert alert = pendingAlert;' 'USB 录音结束后不再补播任务提示音'
require_text "$PROJECT_DIR/src/AudioFeedback.cpp" 'if (codex_usb_mic::streaming()) return;' 'USB 录音在提示音中途开始时未及时停止扬声器'
require_text "$MAIN" 'codex_usb_mic::begin(&audio)' 'V3 未启动 USB 麦克风'
require_text "$MAC_AUDIO" 'TARGET_MIC_NAME = "Codex Macro32 Mic"' 'macOS 自动输入选择没有使用标准 USB 麦克风名'
require_text "$MAC_AUDIO" 'self._backend.set_default_input(target)' '插线后自动选择设备麦克风逻辑丢失'
require_text "$MAC_AUDIO" 'self._restore(devices)' '拔线或关闭功能后恢复原麦克风逻辑丢失'
require_text "$COMPANION" 'auto_select_usb_mic: bool = True' 'Companion 没有默认启用自动 USB 麦克风选择'
require_text "$COMPANION" '"--no-auto-usb-mic"' '命令行缺少关闭自动麦克风选择的通用参数'
require_text "$INSTALL_BRIDGE" 'mac_audio_input.py' '登录自启动安装脚本漏装自动麦克风模块'
require_text "$COMPANION" 'if not self._ble_writer_lock.acquire()' '多个 Companion 实例又会争抢 BLE 写连接'
require_text "$COMPANION" 'self._release_process_lock()' 'Companion 退出时不再释放进程锁'
require_text "$PROCESS_LOCK" 'fcntl.LOCK_EX | fcntl.LOCK_NB' 'Companion 进程锁不是非阻塞独占锁'
require_text "$INSTALL_BRIDGE" 'process_lock.py' '登录自启动安装脚本漏装 BLE 进程锁模块'


require_text "$BLE" 'persistUsage(weeklyLeft, resetSeconds);' '额度 NVS 缓存丢失'
require_text "$BLE" 'constexpr char kFirmwareVersion[] = "3.0.0-waveshare-1.85b";' 'V3 固件版本标识错误'
require_text "$BLE" 'persistCompanionConfig' '同步间隔/声音设置持久化丢失'
forbid_text "$BLE" 'companion_->notify();' 'Companion 写回调又同步发送通知，可能导致连续数据包断连'
require_text "$UI" 'if (state.weeklyLeft >= 0)' '真实额度显示逻辑丢失'
forbid_text "$UI" 'companionQuotaEnabled' '又加入了关闭额度显示的设备逻辑'
forbid_text "$UI" 'updateThemePalette' '又加入了多主题换色逻辑'
require_text "$BLE" 'nvs_flash_erase();' '恢复出厂逻辑丢失'
require_text "$BLE" 'BLEDevice::startAdvertising();' '蓝牙广播逻辑丢失'
require_text "$MAIN" 'if (audioReady && !audio.muted()) audio.readyChime();' '开机提示音自检丢失'
require_text "$MAIN" 'imuState.filtered.zG <= kFaceDownEnterZG' '翻面静音极性又被改反'
forbid_text "$MAIN" 'applyDisplayRotation' '自动旋转实现又被加入主程序'
forbid_text "$MAIN" 'updateDisplayRotation' '自动旋转更新又被加入主程序'
forbid_text "$LVGL_PORT" 'lv_disp_set_rotation' 'LVGL 自动旋转入口又被启用'

echo "回归检查通过：核心功能、显示约束、录音、设置和连接入口均存在（自动旋转已关闭）。"
