#include <Arduino.h>
#include "communicate.hpp"  // 在这里引入我们编写的串口通信文件


void setup() {
  serial_start();
}

void loop() {
  serial_loop();
}