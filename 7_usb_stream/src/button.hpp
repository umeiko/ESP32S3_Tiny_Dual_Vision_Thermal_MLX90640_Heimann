#pragma once

#include <Arduino.h>

#define BTN_LONG_PUSH_T 1000
#define BUTTON_PIN      0
#define BUTTON_PIN_1    1
#define BUTTON_TRIG_LEVEL LOW

struct ButtonState {
    unsigned long pushed_start_time = 0;
    bool          pushed            = false;
    bool          long_pushed       = false;
};

static ButtonState btn_state_0;
static ButtonState btn_state_1;

void button_init() {
    pinMode(BUTTON_PIN, INPUT_PULLUP);
    pinMode(BUTTON_PIN_1, INPUT_PULLUP);
}

void func_button_pushed(int pin_number) {
    Serial.printf("[button] button pushed on pin %d \n", pin_number);
    current_disp = !current_disp;
    disp_changed = true;
}

void func_button_long_pushed(int pin_number) {
    Serial.printf("[button] button long pushed on pin %d \n", pin_number);
    current_disp = !current_disp;
    disp_changed = true;
}

void button_loop_on_pin(int pin_number, ButtonState& state) {
    if (digitalRead(pin_number) == BUTTON_TRIG_LEVEL) {
        // 首次检测到按下瞬间记录起始时间
        if (!state.pushed) {
            state.pushed_start_time = millis();
        }

        // 长按检测
        if (millis() - state.pushed_start_time >= BTN_LONG_PUSH_T) {
            if (!state.long_pushed) {
                func_button_long_pushed(pin_number);
                state.long_pushed = true;
            }
        }

        vTaskDelay(5);
        if (digitalRead(pin_number) == BUTTON_TRIG_LEVEL) {
            state.pushed = true;
        }
    } else {
        // 释放瞬间：如果曾经确认过按下且未触发长按，则报短按
        if (state.pushed) {
            if (!state.long_pushed) {
                func_button_pushed(pin_number);
            }
        }
        state.pushed      = false;
        state.long_pushed = false;
    }
}

void button_loop() {
    button_loop_on_pin(BUTTON_PIN,   btn_state_0);
    button_loop_on_pin(BUTTON_PIN_1, btn_state_1);
}
