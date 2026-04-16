#pragma once

#include <Arduino.h>
#include <EEPROM.h>
#include "shared_val.h"

#include "mlx_drivers/mlx_probe.hpp"


void sensor_detect_and_init(){
    if (blocking_mlx_init_and_check(4)) {
        sensor_status = CONNECTED;
    } else {
        sensor_status = DISCONNECTED;
    }
}

void sensor_loop(){
    probe_loop_mlx();
}


