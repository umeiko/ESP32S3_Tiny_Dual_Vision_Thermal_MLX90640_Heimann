#include <Arduino.h>
#include <EEPROM.h>
#include "communicate.hpp"
#include "screen.hpp"
#include "touch.hpp"
#include "sensor_hal.hpp"
#include "button.hpp"
#include "draw.hpp"
#include "camera.hpp"


bool start_sensor = false;
void setup1(){
  while (start_sensor == false){delay(100);}
  logf("Task1 running on core: %d", xPortGetCoreID());
  sensor_detect_and_init();
  button_init();
  sensor_loop();  // 更新一张画面出来
}

void loop1(){
  sensor_loop();
  button_loop();
}

// 定义任务句柄
TaskHandle_t Task1;
void vTaskCore0(void * pvParameters){
  setup1();
  for(;;){
    loop1();
  }
}

void setup() {
  serial_start();
  xTaskCreatePinnedToCore(
                  vTaskCore0,   /* 任务函数 */
                  "vTaskCore0",     /* 任务名称 */
                  10000,       /* 堆栈大小 */
                  NULL,        /* 参数 */
                  1,           /* 优先级 */
                  &Task1,      /* 任务句柄 */
                  0);          /* 指定核心: 0 */
  screen_init();
  touch_setup();
  camera_init();
  delay(100);           // 给电源和时钟稳定的时间
  start_sensor = true;  // 让核心0的任务开始运行
  unsigned long wait_start = millis();
  draw_connecting_screen();
  smooth_on();
  while (millis() - wait_start < 2000) {
    if (sensor_status == CONNECTED){
      logf("Sensor detected!");
      draw_connected_screen();
      break;
    }
  }
  if (sensor_status != CONNECTED) {
    logf("Sensor not detected. Showing no signal screen.");
    draw_nosignal_screen();
  }
  smooth_off();
  screen_loop();
  smooth_on();
}

void loop() {
  serial_loop();
  touch_loop();  // 如果不接触摸屏的话一定要注释这个
  if (is_composite_streaming){
    draw_streaming_screen();
  }else{
    if (current_disp == DISP_THERMAL) {
      screen_loop();
    }else{
      camera_loop();
    }
  }
}
