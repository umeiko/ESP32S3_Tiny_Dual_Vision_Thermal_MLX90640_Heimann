#include <Arduino.h>
#include <EEPROM.h>
#include "communicate.hpp"
#include "screen.hpp"
#include "touch.hpp"
#include "sensor_hal.hpp"
#include "button.hpp"
#include "draw.hpp"
#include "camera.hpp"
#include "networks.hpp"
#include "webserver.hpp"

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
  network_init();
  touch_setup();
  camera_init();
  ws_init();
  delay(800);           // 给电源和时钟稳定的时间
  start_sensor = true;  // 让核心0的任务开始运行
  unsigned long wait_start = millis();
  while (sensor_status != CONNECTED && millis() - wait_start < 500) {
    delay(500);
  }
  if (sensor_status != CONNECTED) {
    logf("Sensor not detected. Showing no signal screen.");
    draw_nosignal_screen();
  } else {
    logf("Sensor detected! Starting screen loop.");
    screen_loop();
  }
  smooth_on();
}

void loop() {
  serial_loop();
  touch_loop();
  if (current_disp == DISP_THERMAL) {
      screen_loop();
    }else{
      camera_loop();
    }
  ws_loop();
}
