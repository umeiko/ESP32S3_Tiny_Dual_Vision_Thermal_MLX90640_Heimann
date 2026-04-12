#ifndef DRAW_H
#define DRAW_H

#include <Arduino.h>
#include "screen.hpp"
#include "shared_val.hpp"
#include "color_map.hpp"
#include "BilinearInterpolation.hpp"
#include "mlx_drivers/mlx_probe.hpp"

const int biox = 32;
const int bioy = 24;
// 使用 pushImage 渲染整帧
// uint16_t frameBuffer[biox * PROB_SCALE * bioy * PROB_SCALE]; // 320x240 帧缓冲区 - 移到 PSRAM
const int lines = 3;  // 一次渲染多少行的像素
uint16_t  lineBuffer[32 * 10 * lines]; // Toggle buffer for lines
uint16_t  dmaBuffer1[32 * 10 * lines]; // Toggle buffer for lines
uint16_t  dmaBuffer2[32 * 10 * lines]; // Toggle buffer for lines
uint16_t* dmaBufferPtr = dmaBuffer1;
bool dmaBufferSel = 0;


// 绘制十字
inline void draw_cross(int x, int y, int len){
   tft.drawLine(x - len/2, y, x + len/2, y, tft.color565(255, 255, 255));
   tft.drawLine(x, y-len/2, x, y+len/2,  tft.color565(255, 255, 255));

   tft.drawLine(x - len/4, y, x + len/4, y, tft.color565(0, 0, 0));
   tft.drawLine(x, y-len/4, x, y+len/4,  tft.color565(0, 0, 0));
}

// 点测温功能
inline void show_local_temp(int x, int y, int cursor_size){
   draw_cross(x, y, 8);
   float* tempBuffer = (float*)pReadBuffer;
   float temp_xy = tempBuffer[(24 - y / (int)mlx_scale()) * 32 + (x / (int)mlx_scale())];
   int shift_x, shift_y;
   if (x<140){shift_x=10;} else {shift_x=-60;}
   if (y<120){shift_y=10;} else {shift_y=-20;}
   tft.setTextSize(2);
   tft.setCursor(x+shift_x, y+shift_y);
   tft.printf("%.2f", temp_xy);
}  

// 点测温功能
inline void show_local_temp(int x, int y){
   show_local_temp(x, y, 2);
}



void draw(){
   static int value;
   int now_y = 0;
   int cols = mlx_cols();
   int rows = mlx_rows();

   int scale = (int)mlx_scale();
   int render_w = cols * scale;
   int render_h = rows * scale;
   // 图像的渲染流程
   if(use_upsample){
      // 使用双线性插值渲染整帧到缓冲区
      init_interp_tables(cols, rows, scale);
      tft.startWrite();
      for(int y=0; y<24 * scale; y++){
         for(int x=0; x<32 * scale; x++){
            value = bio_linear_interpolation(x, render_h - 1 - y, mlx90640To_buffer, cols, rows);
            lineBuffer[x + now_y * render_w] = colormap[value];
         }
         now_y++;
         if (now_y == lines) {
            dmaBufferPtr = dmaBufferSel ? dmaBuffer2 : dmaBuffer1;
            dmaBufferSel = !dmaBufferSel;
            // 直接画在 (0, y-now_y+1) 位置，宽 render_w
            tft.pushImageDMA(0, y - now_y + 1, render_w, lines, lineBuffer, dmaBufferPtr);
            now_y = 0;
         }
      }
      if (now_y != 0) {
         dmaBufferPtr = dmaBufferSel ? dmaBuffer2 : dmaBuffer1;
         dmaBufferSel = !dmaBufferSel;
         tft.pushImageDMA(0, render_h - now_y, render_w, now_y, lineBuffer, dmaBufferPtr);
         now_y = 0;
      }
      tft.endWrite(); 
   }else{
      static uint16_t c565;
      tft.startWrite();
      for (int i = 0; i < 24 ; i++){
         for (int j = 0; j < 32; j++){
            c565 = colormap[mlx90640To_buffer[(23-i)*32 + j]];
            tft.fillRect(j * scale, (i * scale), scale, scale, c565);
         }
      }
      tft.endWrite();
   }
   // 绘制热点追踪十字
   if (flag_trace_max==true) {draw_cross(y_max * scale, (23-x_max) * scale, 8);}
   // 绘制温度采集光标
   if (flag_show_cursor==true) {show_local_temp(test_point[0], test_point[1]);}
}

// 用来处理画面被暂停时的热成像图层的渲染工作
void freeze_handeler(){
   // 仅拍照模式下，位于第一屏时会启用这个功能
   if (flag_clear_cursor) {draw(); flag_clear_cursor=false;} // 通过重新渲染一张画面来清除光标
   if (flag_show_cursor) {
      show_local_temp(test_point[0], test_point[1]);
   } // 每次点击都渲染光标位置
}


// 探头准备期间的渲染管线
void preparing_loop(){
   tft.setRotation(1);
   if (prob_status == PROB_CONNECTING){
      tft.setCursor(40, 180);
      tft.setTextColor(TFT_WHITE, TFT_BLACK, true);
      tft.setTextSize(1);
      tft.fillRect(40, 180, 220, 30, TFT_BLACK); 
      tft.printf("Triying to connect to MLX...");
      tft.setCursor(80, 190);
      tft.printf("address: %d\n", 0x33);
      delay(10);
   }else if(prob_status == PROB_INITIALIZING){
      tft.setCursor(40, 180);
      tft.setTextColor(TFT_WHITE, TFT_BLACK, true);
      tft.setTextSize(1);
      tft.fillRect(40, 180, 200, 30, TFT_BLACK); 
      tft.printf("MLX... is ready, initializing...\n");
      delay(10);
   }else if(prob_status == PROB_PREPARING){
      tft.setCursor(40, 180);
      tft.setTextColor(TFT_WHITE, TFT_BLACK, true);
      tft.setTextSize(1);
      tft.fillRect(40, 180, 200, 30, TFT_BLACK); 
      tft.printf("MLX... initializing... \n"); 
      delay(10);
   }
}

// 准备期间的渲染管线
void refresh_status(uint8_t tp_status){
   if (tp_status == TP_CONNECTING){
      tft.setCursor(40, 180);
      tft.setTextColor(TFT_WHITE, TFT_BLACK, true);
      tft.setTextSize(1);
      tft.fillRect(40, 180, 220, 30, TFT_BLACK); 
      tft.printf("prepering touch panel...");
      delay(10); 
   }else if (tp_status == TP_READY){
      tft.setCursor(40, 180);
      tft.setTextColor(TFT_WHITE, TFT_BLACK, true);
      tft.setTextSize(1);
      tft.fillRect(40, 180, 220, 30, TFT_BLACK); 
      tft.printf("touch panel connected...");
      delay(10); 
   }else if (tp_status == TP_NOTFOUND){
      tft.setCursor(40, 180);
      tft.setTextColor(TFT_WHITE, TFT_BLACK, true);
      tft.setTextSize(1);
      tft.fillRect(40, 180, 220, 30, TFT_BLACK); 
      tft.printf("touch panel notfound...");
      delay(10); 
   }
}

// 处理tft_espi渲染管线
void screen_loop(){
   static unsigned long dt;
   if(!flag_in_photo_mode){
      dt = millis();
      while (prob_lock == true) {delay(5);}
      pix_cp_lock = true;
      float* tempBuffer = (float*)pReadBuffer;
      for (int i = 0; i < 768; i++) {  // 拷贝温度信息, 并提前映射到色彩空间中
         // 先用一个临时 int 变量计算
         int temp_val = (int)(180.0 * (tempBuffer[i] - T_min_fp) / (T_max_fp - T_min_fp));
         // 必须同时限制【下限】和【上限】
         if (temp_val < 0) temp_val = 0;       // 防止低温变成负数导致溢出
         if (temp_val > 179) temp_val = 179;   // 防止高温越界
         // 最后安全赋值给 uint16_t 数组
         mlx90640To_buffer[i] = (uint16_t)temp_val;
      }
      pix_cp_lock = false;
      draw();
      dt = millis() - dt;
      if (flag_show_cursor==true){
         tft.setTextSize(1);
         tft.setCursor(25, 220);
         tft.printf("max: %.2f  ", T_max_fp);
         tft.setCursor(25, 230);
         tft.printf("min: %.2f  ", T_min_fp);

         tft.setCursor(105, 220);
         tft.printf("avg: %.2f  ", T_avg_fp);
         tft.setCursor(105, 230);
         tft.printf("bat: %d %% ", vbat_percent);

         tft.setCursor(180, 220);
         tft.printf("bright: %d  ", brightness);
         tft.setCursor(180, 230);
         tft.printf("%d ms", dt);
      }
      while (cmap_loading_lock == true) {delay(1);} 
   }else{
      freeze_handeler();
   }
}



#endif