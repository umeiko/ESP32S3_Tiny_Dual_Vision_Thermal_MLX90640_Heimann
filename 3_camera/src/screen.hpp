
#pragma once
#include <TFT_eSPI.h>
#include <TJpg_Decoder.h>
#include <Arduino.h>
#include "miku_jpg.hpp"

#define PIN_BLK 6
uint8_t brightness = 185; // Default brightness level

static const uint16_t screenWidth  = 280;
static const uint16_t screenHeight = 240;

TFT_eSPI tft = TFT_eSPI(screenHeight, screenWidth); /* TFT instance */

// ================= 亮度控制 =================
inline void set_brightness(int _brightness, bool remenber=true){
   if (_brightness < 255 && _brightness > 5){
      analogWrite(PIN_BLK, _brightness);
      if (remenber) {brightness = _brightness;}
   }else if(_brightness >= 255) 
    {
        digitalWrite(PIN_BLK, HIGH);
        if (remenber){brightness = _brightness;}
   }else if(_brightness <= 5)
   {
    analogWrite(PIN_BLK, 5); 
    if(remenber){brightness = _brightness;}
   }
}

// ================= 亮起屏幕 =================
void smooth_on(){
   digitalWrite(PIN_BLK, LOW);
   for(int i=0; i<brightness; i++){
      set_brightness(i, false);
      delay(2);
   }
}

// ================= 熄灭屏幕 =================
void smooth_off(){
   analogWrite(PIN_BLK, brightness);
   for(int i=brightness; i>=0; i--){
      analogWrite(PIN_BLK, i);
      delay(2);
   }
}

// ================= JPEG 解码回调 =================
bool tft_output(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t* bitmap){
    // 用途就是把解码后的图像数据绘制到屏幕上
   if ( y >= tft.height() ) return 0;
   tft.pushImage(x, y, w, h, bitmap);
   return 1;
}

// ================= 绘制图片 =================
void render_miku(){

   TJpgDec.setCallback(tft_output);  // 设置jpeg解码器回调函数
   tft.fillScreen(TFT_BLACK);
   TJpgDec.setJpgScale(1);
   uint16_t w = 0, h = 0;
   TJpgDec.getJpgSize(&w, &h, miku_jpg, sizeof(miku_jpg));
   tft.startWrite();
   TJpgDec.drawJpg(0, 0, miku_jpg, sizeof(miku_jpg));
   tft.endWrite();
}

// ================= 屏幕初始化主函数 =================
void screen_init(){
    Serial.println("Initializing screen...");
    // 初始化屏幕背光的控制引脚
    pinMode(PIN_BLK, OUTPUT);
    digitalWrite(PIN_BLK, LOW);
    
    tft.init();
    tft.setRotation(3); 
    tft.setSwapBytes(true);
    tft.invertDisplay(true); 

    // 打开屏幕之后要做的事情：慢慢打开屏幕，然后绘制一个可爱的miku图片
    render_miku();
    delay(300);
    smooth_on();
    delay(500);
    smooth_off();
    tft.setRotation(0);
    tft.fillScreen(TFT_BLACK);
}

// 支持的命令：screen off, screen on, screen brightness <value>
void screen_cli(String cmd){
    cmd.trim();
    if (cmd.equalsIgnoreCase("screen off")){
        smooth_off();
        // 增加熄屏回显
        Serial.println("[Screen] Turned OFF smoothly.");
        
    }else if(cmd.equalsIgnoreCase("screen on")){
        smooth_on();
        // 增加亮屏回显
        Serial.println("[Screen] Turned ON smoothly.");
        
    }else if(cmd.startsWith("screen brightness ")){
        String valueStr = cmd.substring(String("screen brightness ").length());
        int value = valueStr.toInt();
        set_brightness(value);
        // 增加亮度设置回显，把实际设置的值打印出来
        Serial.print("[Screen] Brightness set to: ");
        Serial.println(value);
        
    }else{
        // 未知指令提示
        Serial.println("[Error] Unknown screen command: " + cmd);
    }
}

// 在屏幕底部 40px 黑边区域绘制 FPS 与内存信息
void draw_camera_overlay(float fps, size_t free_heap, size_t free_psram, size_t frame_len){
    const int bar_y = 240;
    const int bar_h = 40;
    const int bar_w = 240;
    
    // 清空底部信息条背景（只清中间安全区，保留最边缘像素避免闪）
    tft.fillRect(0, bar_y, bar_w, bar_h, TFT_BLACK);
    
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setTextSize(1);
    tft.setTextDatum(MC_DATUM); // 以坐标为文字中心，方便居中避开圆角
    
    size_t total_internal = heap_caps_get_total_size(MALLOC_CAP_INTERNAL);
    size_t total_psram = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
    float heap_pct = total_internal ? ((float)(total_internal - free_heap) * 100.0f / total_internal) : 0.0f;
    float psram_pct = total_psram ? ((float)(total_psram - free_psram) * 100.0f / total_psram) : 0.0f;
    float sz_kb = frame_len / 1024.0f;
    
    int cx = bar_w / 2;
    int y = bar_y + 12; // 位于黑边上半部分，避开底部圆角
    
    tft.drawString("FPS:" + String(fps, 1) + "  RAM:" + String(heap_pct, 1) + "%  PSRAM:" + String(psram_pct, 1) + "%", cx, y);
    y += 14;
    tft.drawString("Image Size:" + String(sz_kb, 2) + " kb", cx, y);
    
    tft.setTextDatum(TL_DATUM); // 恢复默认左上角基准
}