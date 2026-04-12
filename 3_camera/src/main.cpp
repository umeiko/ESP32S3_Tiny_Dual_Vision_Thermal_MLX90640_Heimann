#include <Arduino.h>
#include "communicate.hpp"
#include "screen.hpp"
#include "camera.hpp"

void setup() {
  serial_start();
  camera_init();
  screen_init();

  // 先渲染一帧画面再点亮屏幕，比较优雅
  camera_loop();
  smooth_on();
}

void loop() {
  serial_loop();
  camera_loop();
}