#include <Arduino.h>
#include "communicate.hpp"
#include "screen.hpp"

void setup() {
  serial_start();
  screen_init();
}

void loop() {
  serial_loop();
}