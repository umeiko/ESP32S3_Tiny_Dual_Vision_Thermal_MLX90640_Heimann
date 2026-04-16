#pragma once
#include <Arduino.h>
#include <Wire.h>
TwoWire *probeWire = &Wire;
TwoWire *touchWire = &Wire1;

bool flag_in_photo_mode = false;  // 是否在拍照模式（即屏幕显示 MLX90640 的数据）下
#define MLX_SDA  42
#define MLX_SCL  41

// 定义传感器类型
enum {
    SENSOR_NONE,
    SENSOR_HEIMANN_32X32,
    SENSOR_MLX90640, // 32x24
    SENSOR_MLX90641  // 16x12
};  // 传感器类型枚举
uint8_t current_sensor = SENSOR_MLX90640;
// 传感器状态
enum {
    CHECKING,
    CONNECTED, 
    DISCONNECTED 
};
uint8_t sensor_status = CHECKING;

// ================= MLX双缓冲变量定义 =================
// 实际的物理内存
uint16_t* frameBuffer = nullptr;  // 在 PSRAM 中分配
float *mlxBufferA = nullptr;
float *mlxBufferB = nullptr;

uint16_t *mlx90640To_buffer = nullptr;

// 读写指针 (volatile 防止编译器过度优化)
volatile float* pWriteBuffer = nullptr; // Core 1 (探头) 往这里写
volatile float* pReadBuffer = nullptr;  // Core 0 (屏幕) 从这里读

volatile bool hasNewData = false;
// 互斥锁 (使用 FreeRTOS 信号量)
SemaphoreHandle_t swapMutex = nullptr;
// 温度浮点数
float T_min_fp = 0, T_max_fp = 0, T_avg_fp = 0;
uint8_t x_max, y_max, x_min, y_min;
bool prob_lock = false;
unsigned short T_max, T_min;// 温度
unsigned long  T_avg; // 温度平均值
float ft_max, ft_min; // 温度浮点数