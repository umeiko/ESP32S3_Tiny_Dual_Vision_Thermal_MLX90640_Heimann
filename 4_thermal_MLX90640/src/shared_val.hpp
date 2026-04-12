#ifndef SHARED_VAL_H
#define SHARED_VAL_H

#include <Arduino.h>
#include <Wire.h>
#include <EEPROM.h>

#define MLX90640_PIXELS 768
#define MLX90641_PIXELS 192
#define MLX_MAX_PIXELS MLX90640_PIXELS
#define MLX90640_COLS 32
#define MLX90640_ROWS 24
#define MLX90641_COLS 16
#define MLX90641_ROWS 12

// ====================== 引脚定义 =======================

// I2C 引脚定义
#define MLX_VDD  19
#define MLX_SDA  23
#define MLX_SCL  18

// ====================== 引脚定义 =======================

// 定义传感器类型
enum {
    SENSOR_MLX90640, // 32x24
    SENSOR_MLX90641  // 16x12
};  // 传感器类型枚举

 uint8_t current_sensor = SENSOR_MLX90640;
 uint8_t SRC_WIDTH = 32;
 uint8_t SRC_HEIGHT = 24;

enum  {
    PROB_CONNECTING,
    PROB_INITIALIZING,
    PROB_PREPARING,
    PROB_READY
}; // 探头目前的启动状态码

// 触摸屏状态码
enum {
    TP_CONNECTING,
    TP_READY,
    TP_NOTFOUND
};

// 共享变量
 uint8_t brightness = 185;  // 屏幕亮度
 unsigned short T_max = 0, T_min = 0;// 温度
 unsigned long  T_avg = 0; // 温度平均值
 float ft_max = 0, ft_min = 0; // 温度浮点数

// 温度浮点数
 float T_min_fp = 0, T_max_fp = 0, T_avg_fp = 0;

// 新增：动态控制变量
 int SENSOR_ROWS = 24;   // 当前传感器高度
 int SENSOR_OFFSET = 0; // 垂直偏移量

// ================= 双缓冲变量定义 =================
// 实际的物理内存
 uint16_t* frameBuffer = nullptr;  // 在 PSRAM 中分配
 float *mlxBufferA = nullptr;
 float *mlxBufferB = nullptr;

 uint16_t *mlx90640To_buffer = nullptr;

// 读写指针 (volatile 防止编译器过度优化)
 volatile float* pWriteBuffer = nullptr; // Core 1 (探头) 往这里写
 volatile float* pReadBuffer = nullptr;  // Core 0 (屏幕) 从这里读

 volatile bool hasNewData = false;

 uint16_t test_point[2] = {140, 120};  // 测温点的位置
 bool flag_use_kalman = true;  // 是否使用卡尔曼滤波器
 bool use_upsample = true;  // 是否使用双线性插值
 bool flag_trace_max = true;  // 是否使用最热点追踪
 bool flag_in_photo_mode = false;  // 是否正处于照相模式(画面暂停)
 bool flag_show_cursor = true; // 是否显示温度采集指针
 bool flag_clear_cursor = false; // 是否清除光标
 bool flag_sensor_ok = false;  // 传感器是否初始化成功
 uint8_t prob_status = PROB_CONNECTING;  // 探头当前状态

// 互斥锁 (使用 FreeRTOS 信号量)
 SemaphoreHandle_t swapMutex = nullptr;

// I2C 接口指针
TwoWire *probeWire = &Wire;

// 热点追踪坐标
 int x_max = 0;
 int y_max = 0;

// 锁标志
 volatile bool prob_lock = false;
 volatile bool pix_cp_lock = false;
 volatile bool cmap_loading_lock = false;

// 电池电量百分比
 int vbat_percent = 100;

// 帧计数
 int num_frames = 0;


 bool color_reverse = true;  // 是否反转色彩
// 展示剩余内存, 总内存，以及使用的百分比情况
// 以kb为单位展示
 void print_heap_usage() {
    size_t free_internal = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    size_t total_internal = heap_caps_get_total_size(MALLOC_CAP_INTERNAL);

    Serial.printf("Internal heap: %u / %u bytes (%.2f%% used)\n",
        (unsigned int)(total_internal - free_internal), (unsigned int)total_internal,
        total_internal ?
        (float)(total_internal - free_internal) * 100.0f / total_internal : 0.0f);

    size_t free_psram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    size_t total_psram = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);

    Serial.printf("PSRAM: %u / %u bytes (%.2f%% used)\n",
        (unsigned int)(total_psram - free_psram), (unsigned int)total_psram,
        total_psram ?
        (float)(total_psram - free_psram) * 100.0f / total_psram : 0.0f);

}
#endif