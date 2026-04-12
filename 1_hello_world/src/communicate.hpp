#pragma once
#include <Arduino.h>

void serial_start() {
  Serial.begin(115200);
  while (!Serial) {
    ; // 等待串口连接
  }
  Serial.println("Serial communication initialized.");
}

// 支持 h 指令，查看帮助信息，支持 echo 指令，回显输入内容 其它指令返回未知并且打印帮助信息
void serial_loop(){
    if (Serial.available() > 0) {
        String input = Serial.readStringUntil('\n');
        input.trim(); // 去除输入两端的空白字符
        if (input == "h") {
            Serial.println("Available commands:");
            Serial.println("h - Show this help message");
            Serial.println("echo <message> - Echo the input message");
        } else if (input.startsWith("echo ")) {
            String message = input.substring(5); // 提取 echo 后面的内容
            Serial.println("Echo: " + message);
        } else {
            Serial.println("Unknown command. Type 'h' for help.");
        }
    }
}