#include <Arduino.h>
#include <EEPROM.h>
#include "communicate.hpp"
#include "screen.hpp"
#include "touch.hpp"
#include "flappy.hpp"

FlappyBirdGame game(&tft);

void setup() {
  serial_start();
  screen_init();
  EEPROM.begin(512);
  load_flappy_hiscore();
  touch_setup_nolvgl();
  smooth_on();
}

void loop() {
  serial_loop();
  touch_loop();
  game.update();
}
