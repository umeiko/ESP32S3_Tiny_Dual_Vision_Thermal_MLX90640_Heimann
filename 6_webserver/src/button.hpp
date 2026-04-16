#pragma once

#include <Arduino.h>

#define BTN_LONG_PUSH_T 1000
#define BUTTON_PIN 0
#define BUTTON_TRIG_LEVEL LOW

void button_init(){
    pinMode(BUTTON_PIN, INPUT_PULLUP);
}

void func_button_pushed(){

}

void func_button_long_pushed(){

}


void button_loop(){
    static unsigned long btn_pushed_start_time =  0;
    static bool btn_pushed = false;
    static bool btn_long_pushed = false;
    if (digitalRead(BUTTON_PIN) == BUTTON_TRIG_LEVEL){  // 按钮1触发 
        if (millis() - btn_pushed_start_time >= BTN_LONG_PUSH_T){
            if (!btn_long_pushed){
            func_button_long_pushed();
            btn_long_pushed = true;
            }
        }
        vTaskDelay(5);
        if (digitalRead(BUTTON_PIN) == BUTTON_TRIG_LEVEL){btn_pushed=true;}
    }else{
        btn_pushed_start_time = millis();
        if (btn_pushed) {
            if (!btn_long_pushed){func_button_pushed();}
        }
        btn_pushed=false;
        btn_long_pushed = false;
    }
}