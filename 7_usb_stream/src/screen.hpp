
#pragma once
#include <TFT_eSPI.h>
#include <TJpg_Decoder.h>
#include <Arduino.h>
#include "miku_jpg.hpp"

#define PIN_BLK 6
uint8_t brightness = 185; // Default brightness level
#define PWM_CHANNEL 0     // LEDC PWM 通道
#define PWM_FREQ 20000    // PWM 频率 20kHz (减少 I2C 干扰)
#define PWM_RESOLUTION 8  // 8位分辨率 (0-255)
static const uint16_t screenWidth  = 280;
static const uint16_t screenHeight = 240;

TFT_eSPI tft = TFT_eSPI(screenHeight, screenWidth); /* TFT instance */

// ================= 亮度控制 =================
inline void set_brightness(int _brightness, bool remenber=true){
   if (_brightness < 255 && _brightness > 5){
      ledcWrite(PWM_CHANNEL, _brightness);
      if (remenber) {brightness = _brightness;}
   }else if(_brightness >= 255)
    {
        ledcWrite(PWM_CHANNEL, 255);
        if (remenber){brightness = _brightness;}
   }else if(_brightness <= 5)
   {
    ledcWrite(PWM_CHANNEL, 5);
    if(remenber){brightness = _brightness;}
   }
}

// ================= 亮起屏幕 =================
void smooth_on(){
   ledcWrite(PWM_CHANNEL, 0);
   for(int i=0; i<brightness; i++){
      set_brightness(i, false);
      delay(2);
   }
}

// ================= 熄灭屏幕 =================
void smooth_off(){
   ledcWrite(PWM_CHANNEL, brightness);
   for(int i=brightness; i>=0; i--){
      ledcWrite(PWM_CHANNEL, i);
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
    ledcSetup(PWM_CHANNEL, PWM_FREQ, PWM_RESOLUTION);
    ledcAttachPin(PIN_BLK, PWM_CHANNEL);
    
    tft.init();
    tft.setRotation(1); 
    tft.setSwapBytes(true);
    tft.invertDisplay(true); 

    // 打开屏幕之后要做的事情：慢慢打开屏幕，然后miku图片
    render_miku();
    delay(300);
    smooth_on();
    delay(500);
    smooth_off();
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

