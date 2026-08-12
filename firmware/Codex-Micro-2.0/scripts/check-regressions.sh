#!/bin/sh
set -eu

PROJECT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
UI="$PROJECT_DIR/src/CodexUi.cpp"
MAIN="$PROJECT_DIR/src/main.cpp"
BLE="$PROJECT_DIR/src/CodexMicroBle.cpp"
LVGL_PORT="$PROJECT_DIR/src/LvglPort.cpp"
LVGL_INPUT="$PROJECT_DIR/.pio/libdeps/waveshare-1_85b/lvgl/src/core/lv_indev.c"

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
require_text "$UI" '&lv_font_montserrat_32' '中央额度数字没有使用适中字号'
require_text "$UI" 'updateBatteryVisual' '中央电量图标入口丢失'
require_text "$UI" 'quotaAccent' '额度环绿黄红状态色丢失'
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
require_text "$UI" 'if (centerLongPressStage == 0)' '中央不足 0.8s 防误触逻辑丢失'
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
require_text "$UI" 'if (state.bleConnected)' '蓝牙连接状态判断丢失'
forbid_text "$UI" 'if (state.companionReady)' '额度文案又错误依赖 Companion 已同步状态'
require_text "$UI" 'lv_label_set_text(usageLabel, "等待连接");' '未连接等待文字缺失'
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


require_text "$BLE" 'persistUsage(weeklyLeft, resetSeconds);' '额度 NVS 缓存丢失'
require_text "$BLE" 'persistCompanionConfig' '同步间隔/声音设置持久化丢失'
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
