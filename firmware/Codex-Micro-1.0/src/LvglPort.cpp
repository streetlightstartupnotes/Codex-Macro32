#include "LvglPort.h"

#include <Arduino.h>
#include <esp_heap_caps.h>
#include <esp_timer.h>
#include <lvgl.h>

#include "Display_ST77916.h"
#include "Touch_CST816.h"

namespace {

constexpr uint32_t kTickMs = 5;
constexpr size_t kBufferPixels = EXAMPLE_LCD_WIDTH * 48;

lv_disp_draw_buf_t drawBuffer;
lv_color_t* buffer1 = nullptr;
lv_color_t* buffer2 = nullptr;

void flushDisplay(lv_disp_drv_t* driver, const lv_area_t* area,
                  lv_color_t* colors) {
  LCD_addWindow(area->x1, area->y1, area->x2, area->y2,
                reinterpret_cast<uint16_t*>(&colors->full));
  lv_disp_flush_ready(driver);
}

void readTouch(lv_indev_drv_t*, lv_indev_data_t* data) {
  Touch_Read_Data();
  if (touch_data.points != 0) {
    data->point.x = constrain(touch_data.x, 0, EXAMPLE_LCD_WIDTH - 1);
    data->point.y = constrain(touch_data.y, 0, EXAMPLE_LCD_HEIGHT - 1);
    data->state = LV_INDEV_STATE_PR;
  } else {
    data->state = LV_INDEV_STATE_REL;
  }
  touch_data.points = 0;
  touch_data.gesture = NONE;
}

void tick(void*) { lv_tick_inc(kTickMs); }

}  // namespace

void LvglPort_Init() {
  lv_init();

  buffer1 = static_cast<lv_color_t*>(heap_caps_malloc(
      kBufferPixels * sizeof(lv_color_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  buffer2 = static_cast<lv_color_t*>(heap_caps_malloc(
      kBufferPixels * sizeof(lv_color_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (buffer1 == nullptr || buffer2 == nullptr) {
    Serial.println("LVGL PSRAM allocation failed");
    abort();
  }

  lv_disp_draw_buf_init(&drawBuffer, buffer1, buffer2, kBufferPixels);

  static lv_disp_drv_t displayDriver;
  lv_disp_drv_init(&displayDriver);
  displayDriver.hor_res = EXAMPLE_LCD_WIDTH;
  displayDriver.ver_res = EXAMPLE_LCD_HEIGHT;
  displayDriver.flush_cb = flushDisplay;
  displayDriver.draw_buf = &drawBuffer;
  lv_disp_drv_register(&displayDriver);

  static lv_indev_drv_t touchDriver;
  lv_indev_drv_init(&touchDriver);
  touchDriver.type = LV_INDEV_TYPE_POINTER;
  touchDriver.read_cb = readTouch;
  lv_indev_drv_register(&touchDriver);

  const esp_timer_create_args_t timerArgs = {
      .callback = tick,
      .arg = nullptr,
      .dispatch_method = ESP_TIMER_TASK,
      .name = "lvgl",
      .skip_unhandled_events = true,
  };
  esp_timer_handle_t timer = nullptr;
  ESP_ERROR_CHECK(esp_timer_create(&timerArgs, &timer));
  ESP_ERROR_CHECK(esp_timer_start_periodic(timer, kTickMs * 1000));
}

void LvglPort_Loop() { lv_timer_handler(); }

