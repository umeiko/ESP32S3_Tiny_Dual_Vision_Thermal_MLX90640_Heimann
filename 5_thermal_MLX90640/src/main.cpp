#include <Arduino.h>
#include <EEPROM.h>
#include "communicate.hpp"
#include "screen.hpp"
#include "shared_val.hpp"
#include "draw.hpp"
#include "bat.hpp"
#include "mlx_drivers/mlx_probe.hpp"

void setup() {
  bat_init();
  sensor_power_on();
  EEPROM.begin(512);  // 初始化 EEPROM
  serial_start();
  screen_init();
  // 设置当前传感器类型
  current_sensor = EEPROM.read(20); // 读取传感器类型
  if (current_sensor > SENSOR_MLX90641){ current_sensor = SENSOR_MLX90640;}
  if (current_sensor == SENSOR_MLX90640) {is_90640 = true;} else {is_90640 = false;}
  // 尝试初始化 MLX 传感器
  sensor_power_on();
  if (blocking_mlx_init_and_check(5)) {
    flag_sensor_ok = true;
    prob_status = PROB_READY;
    probe_loop_mlx();
    screen_loop();
  } else {
    Serial.println("MLX Sensor initialization failed!");
  }
  smooth_on();
}

void loop() {
  serial_loop();
  bat_loop();
  // 如果传感器已准备好，执行探头读取和屏幕渲染
  if (flag_sensor_ok) {
    probe_loop_mlx();   // 读取传感器数据
    screen_loop();      // 渲染屏幕
  } else {
    // 传感器未准备好，显示准备状态
    preparing_loop();
    delay(100);
  }
}