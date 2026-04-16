#pragma once
#include<Arduino.h>
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"
#include "esp_camera.h"
#include "screen.hpp"
#include <Wire.h>
// 摄像头在这块板子上的引脚定义
#define CAM_WIDTH 320
#define CAM_HEIGHT 240
#define PWDN_GPIO_NUM     -1
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM     39
#define PCLK_GPIO_NUM     36
#define SIOD_GPIO_NUM     10
#define SIOC_GPIO_NUM     11
#define VSYNC_GPIO_NUM    18
#define HREF_GPIO_NUM     21
#define D0_GPIO_NUM       34
#define D1_GPIO_NUM       47
#define D2_GPIO_NUM       48
#define D3_GPIO_NUM       33
#define D4_GPIO_NUM       35
#define D5_GPIO_NUM       37
#define D6_GPIO_NUM       38
#define D7_GPIO_NUM       40

bool camera_ok = false;
camera_fb_t *fb = NULL;

unsigned long fps_last_time = 0;
int fps_frame_count = 0;
float fps_current = 0.0f;

// 占位实现：在右侧黑边显示帧率与内存信息（如需自定义 UI 可在此扩展）
inline void draw_camera_overlay(float fps, size_t free_heap, size_t free_psram, size_t frame_len) {
    // 默认空实现，防止链接错误
}

void camera_init(){
    // 关闭欠压检测
    WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0); 
    delay(500);

    camera_config_t config;
    config.ledc_channel = LEDC_CHANNEL_0;
    config.ledc_timer = LEDC_TIMER_0;
    config.pin_d0 = D0_GPIO_NUM;
    config.pin_d1 = D1_GPIO_NUM;
    config.pin_d2 = D2_GPIO_NUM;
    config.pin_d3 = D3_GPIO_NUM;
    config.pin_d4 = D4_GPIO_NUM;
    config.pin_d5 = D5_GPIO_NUM;
    config.pin_d6 = D6_GPIO_NUM;
    config.pin_d7 = D7_GPIO_NUM;
    config.pin_xclk = XCLK_GPIO_NUM;
    config.pin_pclk = PCLK_GPIO_NUM;
    config.pin_vsync = VSYNC_GPIO_NUM;
    config.pin_href = HREF_GPIO_NUM;
    // config.pin_sccb_sda = SIOD_GPIO_NUM;
    // config.pin_sccb_scl = SIOC_GPIO_NUM;
    config.pin_sccb_sda = -1;
    config.pin_sccb_scl = -1;
    config.sccb_i2c_port = 1; // 使用 Wire1 进行 SCCB 通信
    config.pin_pwdn = PWDN_GPIO_NUM;
    config.pin_reset = RESET_GPIO_NUM;
    config.xclk_freq_hz = 10000000;
    config.frame_size = FRAMESIZE_240X240;  // 320*240
    config.pixel_format = PIXFORMAT_JPEG; // for streaming
    config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
    config.fb_location = CAMERA_FB_IN_PSRAM;
    config.jpeg_quality = 15;
    // 让摄像头使用psram来存储摄像头的数据
    if (psramFound()){
        Serial.printf("PSRAM found, using it for camera buffers\n");
        config.fb_count = 2;
        config.grab_mode = CAMERA_GRAB_LATEST;
    }
    else{   
        Serial.printf("PSRAM not found\n");
        config.fb_count = 1;
        config.fb_location = CAMERA_FB_IN_DRAM;
    }
    Serial.println("Initializing camera...");
    esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK)
    {
        Serial.printf("Camera init failed with error: 0x%x", err); // 打印错误码
        delay(100);
        camera_ok = false;
        esp_camera_deinit();
        return;
    }
    camera_ok = true;
    Serial.println("Camera init okay");
    sensor_t *s = esp_camera_sensor_get();
    // 针对 OV3660 的设置
    if (s->id.PID == OV3660_PID)
    {
        s->set_vflip(s, 1);       // flip it back
        s->set_brightness(s, 1);  // up the brightness just a bit
        s->set_saturation(s, -2); // lower the saturation
    }
    // 针对 OV2640 的设置
    if (s->id.PID == OV2640_PID)
    {
        // 旋转 180 度 = 垂直翻转 + 水平镜像
        s->set_vflip(s, 0);   // 开启垂直翻转
        s->set_hmirror(s, 0); // 开启水平镜像
    }
}


void screen_draw_jpeg(const uint8_t* jpg_data, size_t len){
    if (jpg_data == nullptr || len == 0) return;
    TJpgDec.setJpgScale(1);
    TJpgDec.setCallback(tft_output);
    tft.startWrite();
    TJpgDec.drawJpg(0, 0, jpg_data, len);
    tft.endWrite();
}

void camera_loop(){
}

void camera_cli(String command){
    command.trim();
    if (command.startsWith("check_camera")){
        // 工厂测试：检查摄像头连接状态
        Serial.println("=== Camera Connection Test ===");
        // 检查摄像头初始化状态
        if (camera_ok) {
            Serial.println("OK: Camera initialized successfully");
            sensor_t *s = esp_camera_sensor_get();
            if (s) {
                Serial.printf("INFO: Camera PID=0x%04X ", s->id.PID);
                if (s->id.PID == OV2640_PID) Serial.println("(OV2640)");
                else if (s->id.PID == OV3660_PID) Serial.println("(OV3660)");
                else Serial.println("(Unknown)");
            }
        } else {
            Serial.println("FAIL: Camera not initialized");
        }
        Serial.println("==============================");
    }else if (command.startsWith("test_camera")){
        // 工厂测试：测试摄像头获取画面
        Serial.println("=== Camera Capture Test ===");
        if (!camera_ok) {
            Serial.println("FAIL: Camera not initialized");
            Serial.println("===========================");
            return;
        }
        // 尝试获取一帧
        camera_fb_t *test_fb = esp_camera_fb_get();
        if (test_fb) {
            Serial.println("OK: Frame captured successfully");
            Serial.printf("INFO: Frame size=%dx%d, format=%s, length=%d bytes\n",
                          test_fb->width, test_fb->height,
                          (test_fb->format == PIXFORMAT_JPEG) ? "JPEG" : "Other",
                          test_fb->len);
            esp_camera_fb_return(test_fb);
        } else {
            Serial.println("FAIL: Failed to capture frame");
        }
        Serial.println("===========================");
    }else{
        Serial.println("[Error] Unknown camera command: " + command);
    }
}