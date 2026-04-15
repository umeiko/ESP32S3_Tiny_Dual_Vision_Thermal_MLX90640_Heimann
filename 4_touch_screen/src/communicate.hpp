#pragma once
#include <Arduino.h>
#include "screen.hpp"

#define ConsoleInfo Serial

// 在Serial的输出之前自动添加格式
size_t logf(const char *format, ...) {
    size_t n = 0;
    char buf[128];
    va_list args;
    va_start(args, format);
    n = vsnprintf(buf, sizeof(buf), format, args);
    va_end(args);
    // 绿色时间戳
    ConsoleInfo.print("[\033[32m");
    ConsoleInfo.printf("%6lu", millis());
    ConsoleInfo.print("\033[0m]");
    ConsoleInfo.print(buf);
    // 检查末尾是否有换行符
    size_t len = strlen(buf);
    if (len == 0 || buf[len - 1] != '\n') {
        ConsoleInfo.println();
    } 
    return n;
}

// 在Serial的输出之前自动添加格式
void logln(const char *buf) {
    ConsoleInfo.print("[\033[32m");
    ConsoleInfo.printf("%6lu", millis());
    ConsoleInfo.print("\033[0m]");
    ConsoleInfo.println(buf);
}

void serial_start() {
  Serial.setTimeout(0);
  Serial.begin(115200);
  Serial.println("Serial communication initialized.");
}

void print_heap_usage() { 
    size_t free_internal = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    size_t total_internal = heap_caps_get_total_size(MALLOC_CAP_INTERNAL);

    logf("Internal heap: %u / %u bytes (%.2f%% used)\n",
        (total_internal - free_internal), total_internal,
        total_internal ? 
        (float)(total_internal - free_internal) * 100.0f / total_internal : 0.0f);

    size_t free_psram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    size_t total_psram = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);

    logf("PSRAM: %u / %u bytes (%.2f%% used)\n",
        (total_psram - free_psram), total_psram,
        total_psram ? 
        (float)(total_psram - free_psram) * 100.0f / total_psram : 0.0f);
    
}

// Supports 'h' for help menu
// Supports 'echo' for echoing input
// Supports 'screen' commands for display control
void serial_loop(){
    if (Serial.available() > 0) {
        String input = Serial.readStringUntil('\n');
        input.trim(); // Remove leading and trailing whitespaces
        
        if (input == "h") {
            // ==== English Help Menu ====
            Serial.println("\r\n=========================================");
            Serial.println("          ESP32 Serial Console           ");
            Serial.println("=========================================");
            Serial.println("[ General Commands ]");
            Serial.println("  h                     - Show this help message");
            Serial.println("  echo <message>        - Echo the input message back to you");
            Serial.println("  top                   - Show heap and PSRAM usage");
            Serial.println("");
            Serial.println("[ Screen Control ]");
            Serial.println("  screen on             - Turn on the screen smoothly");
            Serial.println("  screen off            - Turn off the screen smoothly");
            Serial.println("  screen brightness <X> - Set brightness level (X: 5~255)");
            Serial.println("");
            Serial.println("=========================================\r\n");
            
        } else if (input.startsWith("echo ")) {
            String message = input.substring(5); // Extract message after "echo "
            Serial.println("Echo: " + message);
            
        } else if (input == "top") {
            print_heap_usage();
            
        } else if (input.startsWith("screen ")) {  // Intercept screen control commands
            screen_cli(input);
            
        } else if (input.length() > 0) { // Prevent blank enter keys from triggering unknown command
            Serial.println("Unknown command: '" + input + "'. Type 'h' for help.");
        }
    }
}