#pragma once
#include <Arduino.h>
#include "screen.hpp"
#include "shared_val.h"
#include "esp_camera.h"

void camera_cli(String command);
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

void thermal_stream(){
    Serial.print("MLX40BEGIN");
    float* tempBuffer = nullptr;
    if (xSemaphoreTake(swapMutex, pdMS_TO_TICKS(15)) == pdTRUE){
         float* tempBuffer = (float*)pReadBuffer; // 从读缓冲区获取数据
         __sync_synchronize();
         // 注意：T_max_fp 和 T_min_fp 由 Core 1 更新，这里直接使用即可
        Serial.write((uint8_t*)&T_max_fp, sizeof(float));
        Serial.write((uint8_t*)&T_min_fp, sizeof(float));
        // 直接写入整个温度数据块（float数组）- 整块内存写入避免卡顿
        Serial.write((uint8_t*)tempBuffer, MLX90640_PIXELS * sizeof(float));
        xSemaphoreGive(swapMutex); // 数据映射完毕，立刻释放锁，让 Core 0 继续算
    }
    Serial.print("MLX40END");
}

void composite_stream(){
    Serial.print("COMPBEGIN");
    float* tempBuffer = nullptr;
    if (xSemaphoreTake(swapMutex, pdMS_TO_TICKS(15)) == pdTRUE){
         float* tempBuffer = (float*)pReadBuffer; // 从读缓冲区获取数据
         __sync_synchronize();
         // 注意：T_max_fp 和 T_min_fp 由 Core 1 更新，这里直接使用即可
        Serial.write((uint8_t*)&T_max_fp, sizeof(float));
        Serial.write((uint8_t*)&T_min_fp, sizeof(float));
        // 直接写入整个温度数据块（float数组）- 整块内存写入避免卡顿
        Serial.write((uint8_t*)tempBuffer, MLX90640_PIXELS * sizeof(float));
        xSemaphoreGive(swapMutex); // 数据映射完毕，立刻释放锁，让 Core 0 继续算
    }
    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) {
        esp_camera_fb_return(fb);
        Serial.print("COMPEND");
        return;
    }
    Serial.write(fb->buf, fb->len);
    esp_camera_fb_return(fb);
    Serial.print("COMPEND");
}

// Supports 'h' for help menu
// Supports 'echo' for echoing input
// Supports 'screen' commands for display control
void serial_loop(){
    static unsigned long streamming_flag_time = millis();
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
            Serial.println("[ Camera Control ]");
            Serial.println("  check_camera          - Factory test: check camera connection status");
            Serial.println("  test_camera           - Factory test: capture one frame");
            Serial.println("  get_camera            - Capture and send one JPEG frame (JPG ... EJPG)");
            Serial.println("");
            Serial.println("[ Stream Control ]");
            Serial.println("  stream                - Start thermal data stream (MLX40BEGIN ... MLX40END)");
            Serial.println("  composite stream      - Start composite thermal+JPEG stream (COMPBEGIN ... COMPEND)");
            Serial.println("=========================================\r\n");
            
        } else if (input.startsWith("echo ")) {
            String message = input.substring(5); // Extract message after "echo "
            Serial.println("Echo: " + message);
            
        } else if (input == "top") {
            print_heap_usage();
            
        } else if (input.startsWith("screen ")) {  // Intercept screen control commands
            screen_cli(input);
            
        } else if (input.startsWith("check_camera") || input.startsWith("test_camera") || input.startsWith("get_camera")) {
            camera_cli(input);
        }else if (input.startsWith("stream")){
            if (sensor_status != CONNECTED){Serial.println("Probe not ready !"); return;}
            if (is_composite_streaming == true){Serial.println("composite is streaming !"); return;}
            streamming_flag_time = millis();
            is_streaming = true;
            Serial.println("streaming started");
        }else if (input.startsWith("composite stream")){
            if (sensor_status != CONNECTED){Serial.println("Probe not ready !"); return;}
            if (camera_ok != true){Serial.println("Camera not ready !"); return;}
            streamming_flag_time = millis();
            is_streaming = false;
            is_composite_streaming = true;
            Serial.println("composite streaming started");
        }else if (input.startsWith("stop_stream")){
           is_streaming=false;
           is_composite_streaming = false;
        }else if (input.length() > 0) { // Prevent blank enter keys from triggering unknown command
            Serial.println("Unknown command: '" + input + "'. Type 'h' for help.");
        }
    }
    // ================= 处理数据流发送(单光) =================
    if (is_streaming && millis() - streamming_flag_time > 1000){
        is_streaming = false;
        Serial.println("streaming stoped");
    }else if (is_streaming){
        thermal_stream();
    }

    // ================= 处理数据流发送(复合双光) =================
    if (is_composite_streaming && millis() - streamming_flag_time > 1000){
        is_composite_streaming = false;
        Serial.println("composite streaming stoped");
    }else if (is_composite_streaming){
        composite_stream();
    }
}