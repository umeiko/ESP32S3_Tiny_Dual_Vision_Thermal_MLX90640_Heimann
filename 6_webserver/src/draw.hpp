#pragma once    

#include <Arduino.h>
#include "screen.hpp"
#include "shared_val.h"
#include "mlx_drivers/mlx_bilinearInterpolation.hpp"
#include "mlx_drivers/mlx_probe.hpp"

const int lines = 3;  // 一次渲染多少行的像素
uint16_t  lineBuffer[32 * 9 * lines]; // Toggle buffer for lines

uint8_t use_upsample = true;
unsigned short max_posx = 0;
unsigned short max_posy = 0;

const uint16_t colormap[180] = {
0x0002, 0x0003, 0x0003, 0x0004, 0x0004, 0x0005, 0x0005, 0x0006, 0x0006, 0x0007, 
0x0007, 0x0008, 0x0008, 0x0009, 0x0009, 0x000a, 0x000a, 0x000b, 0x000b, 0x000c, 
0x000c, 0x000d, 0x000d, 0x000e, 0x000e, 0x000f, 0x000f, 0x0010, 0x0010, 0x0011, 
0x0011, 0x0011, 0x0811, 0x0810, 0x1010, 0x1010, 0x1810, 0x180f, 0x200f, 0x200f, 
0x280f, 0x280e, 0x300e, 0x300e, 0x380e, 0x380d, 0x400d, 0x400d, 0x480d, 0x480c, 
0x500c, 0x500c, 0x580c, 0x580b, 0x600b, 0x600b, 0x680b, 0x680a, 0x700a, 0x700a, 
0x780a, 0x7809, 0x8009, 0x8009, 0x8809, 0x8808, 0x9008, 0x9008, 0x9808, 0x9807, 
0xa007, 0xa007, 0xa807, 0xa806, 0xb006, 0xb006, 0xb806, 0xb805, 0xc005, 0xc005, 
0xc805, 0xc804, 0xd004, 0xd004, 0xd804, 0xd803, 0xe003, 0xe003, 0xe803, 0xe802, 
0xf801, 0xf801, 0xf821, 0xf821, 0xf841, 0xf841, 0xf861, 0xf861, 0xf881, 0xf880, 
0xf8a0, 0xf8a0, 0xf8c0, 0xf8c0, 0xf8e0, 0xf8e0, 0xf900, 0xf900, 0xf920, 0xf920, 
0xf940, 0xf940, 0xf960, 0xf960, 0xf980, 0xf980, 0xf9a0, 0xf9a0, 0xf9c0, 0xf9c0, 
0xf9e0, 0xfa00, 0xfa20, 0xfa60, 0xfa80, 0xfac0, 0xfae0, 0xfb20, 0xfb40, 0xfb80, 
0xfba0, 0xfbe0, 0xfc00, 0xfc20, 0xfc60, 0xfc80, 0xfcc0, 0xfce0, 0xfd20, 0xfd40, 
0xfd80, 0xfda0, 0xfde0, 0xfe00, 0xfe40, 0xfe60, 0xfe80, 0xfec0, 0xfee0, 0xff20, 
0xff40, 0xff41, 0xff62, 0xff63, 0xff64, 0xff65, 0xff66, 0xff67, 0xff88, 0xff89, 
0xff8a, 0xff8b, 0xff8c, 0xff8d, 0xffae, 0xffaf, 0xffb1, 0xffb2, 0xffb3, 0xffb4, 
0xffd5, 0xffd6, 0xffd7, 0xffd8, 0xffd9, 0xffda, 0xfffb, 0xfffc, 0xfffd, 0xfffe
};

void draw_mlx(){
   int value;
   int now_y = 0;
   int cols = mlx_cols();
   int rows = mlx_rows();

   int scale = (int)mlx_scale();
   int render_w = cols * scale;
   int render_h = rows * scale;
// 1. 计算屏幕 X 轴 (对应传感器的列 y_max)
   max_posx = y_max * scale;

   // 2. 计算屏幕 Y 轴 (对应传感器的行 x_max)
   // 注意：通常热成像渲染会有 Y 轴翻转 (rows - 1 - row)
   max_posy = (rows - 1 - x_max) * scale;
   
   // 3. 边界保护 (防止光标画出屏幕外)
   if (max_posx >= render_w) max_posx = render_w - 1;
   if (max_posy >= render_h) max_posy = render_h - 1;
   if (max_posx < 0) max_posx = 0;
   if (max_posy < 0) max_posy = 0;
   // Serial.printf("Rendering MLX image: %dx%d, scale=%d\n", render_w, render_h, scale);
   if(use_upsample){
      // Serial.println("Using upsample for MLX rendering");
      init_interp_tables(cols, rows, scale);
      tft.startWrite();
      for (int y = 0; y < render_h; y++) {
         for (int x = 0; x < render_w; x++) {
            // buffer 里的数据已经是正向的了，直接查表
            value = mlx_bio_linear_interpolation(x, render_h - 1 - y, mlx90640To_buffer, cols, rows);
            lineBuffer[x + now_y * render_w] = colormap[value];
         }
         // Serial.printf("rendering line %d ... ", y);
         // 凑够 lines 行或者是最后一行时，推送到屏幕
         now_y++;
         if (now_y == lines) {
            // 直接画在 (0, y-now_y+1) 位置，宽 render_w
            tft.pushImage(0, y - now_y + 1, render_w, lines, lineBuffer);
            now_y = 0;
         }
    //   insert_temp_cursor_mlx(y); // 触摸点测温
    //   insert_max_cursor_mlx(y);  // 最高温追踪
         
      }
      // 推送剩余的不足 lines 的行
      if (now_y != 0) {
         tft.pushImage(0, render_h - now_y, render_w, now_y, lineBuffer);
         now_y = 0;
      }
      // Serial.println("done.");
      tft.endWrite(); 
   }else{
      // 非插值模式：使用行缓冲 + DMA 推送，避免逐个 fillRect 导致的卡顿
      tft.startWrite();
      // 逐行渲染：每行复制 scale 次并通过 DMA 推送（y 坐标翻转以匹配插值模式）
      for (int src_row = 0; src_row < rows; src_row++){
         // 构建源行数据（放大 scale 倍宽度）
         for (int j = 0; j < cols; j++){
            uint16_t c565 = colormap[mlx90640To_buffer[src_row * cols + j]];
            // 每列重复 scale 次
            for (int sx = 0; sx < scale; sx++){
               lineBuffer[j * scale + sx] = c565;
            }
         }
         
         // y 坐标翻转：从下往上（rows - 1 - src_row）
         int screen_y_start = (rows - 1 - src_row) * scale;
         
         // 该行在屏幕上重复 scale 次高度
         for (int sy = 0; sy < scale; sy++){
            // 推送这一行到屏幕
            tft.pushImage(0, screen_y_start + sy, render_w, 1, lineBuffer);
            // insert_temp_cursor_mlx(screen_y_start + sy);
            // insert_max_cursor_mlx(screen_y_start + sy);
         }
      }
      tft.endWrite();
   }
}

void draw_bottom_bar(){
    tft.startWrite();
    tft.fillRect(0, 216, 280, 24, TFT_BLACK);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setTextSize(2);
    
    // 最低温 - 左侧，避开左下角圆角
    tft.setTextDatum(TL_DATUM);
    tft.drawString("MIN:" + String(ft_min, 1), 20, 220);
    
    // 最高温 - 右侧，避开右下角圆角
    tft.setTextDatum(TR_DATUM);
    tft.drawString("MAX:" + String(ft_max, 1), 260, 220);
    
    // 恢复默认对齐
    tft.setTextDatum(TL_DATUM);
    tft.endWrite();
}

// 【修改】双缓冲适配的 Screen Loop
void screen_loop_mlx(){
   
      // 等待 Core 0 释放数据 - 使用内存屏障确保可见性
      float* tempBuffer = nullptr;
      if (xSemaphoreTake(swapMutex, pdMS_TO_TICKS(15)) == pdTRUE){
      if (hasNewData) {
         // 有新数据：将 float 温度映射为颜色索引
         // 这段代码执行非常快 (微秒级)，不会阻塞 Core 1 太久
         uint16_t pix = mlx_pixel_count();
         float* srcBuffer = (float*)pReadBuffer; // 从读缓冲区获取数据
         __sync_synchronize();
         // 注意：T_max_fp 和 T_min_fp 由 Core 1 更新，这里直接使用即可
         // 为了防止除以0
         float range = T_max_fp - T_min_fp;
         if (range < 1.0) range = 1.0; 

         for (int i = 0; i < pix; i++) {
            float val = srcBuffer[i];
            int mapped = (int)(180.0 * (val - T_min_fp) / range);
            if (mapped < 0) mapped = 0;
            if (mapped > 179) mapped = 179;
            mlx90640To_buffer[i] = mapped;
         }
         hasNewData = false; // 标记已读
      }
      xSemaphoreGive(swapMutex); // 数据映射完毕，立刻释放锁，让 Core 0 继续算
   }
   // 2. 更新 UI 显示用的数值 (ft_max/ft_min)
   // 无论是否有新图像，都应该刷新这些值，或者使用上一帧的值
   ft_max = T_max_fp;
   ft_min = T_min_fp;

   // 3. 绘图 (耗时操作，在锁外进行)
   // 使用 mlx90640To_buffer (本地缓存/颜色缓存) 进行绘制
   draw_mlx();
   
   // 4. 绘制底部信息栏
   draw_bottom_bar();
}

void draw_nosignal_screen(){
    static bool no_signal_shown = false;
    if (!no_signal_shown){
        tft.fillScreen(TFT_BLACK);
        tft.setTextSize(2);
        tft.drawString("NO SIGNAL", tft.width() / 2, tft.height() / 2);
        tft.setTextDatum(TL_DATUM); // 恢复默认
        no_signal_shown = true;
    }

}

void screen_loop(){
    if (flag_in_photo_mode){delay(5); return;}
    if (sensor_status != CONNECTED) {return;}
    if (disp_changed){
      tft.setRotation(1);
      disp_changed = false;
    }
    screen_loop_mlx();
}