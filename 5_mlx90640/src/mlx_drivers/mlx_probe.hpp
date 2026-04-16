#pragma once

#include <Arduino.h>

#include "MLX90640_API.hpp"
#include "MLX90640_I2C_Driver.hpp"
#include "MLX90641_API.hpp"
#include "MLX90641_I2C_Driver.hpp"
#include "../shared_val.h"
#include <esp_heap_caps.h>
#include "mlx_bilinearInterpolation.hpp"


#define MLX90640_PIXELS 768
#define MLX90641_PIXELS 192
#define MLX_MAX_PIXELS MLX90640_PIXELS
#define MLX90640_COLS 32
#define MLX90640_ROWS 24
#define MLX90641_COLS 16
#define MLX90641_ROWS 12

// 缩放相关：基础缩放和最大缩放. MLX90640 使用 BASE_SCALE，MLX90641 需要放大 2 倍以匹配显示效果
#define BASE_SCALE 9
#define MAX_SCALE (BASE_SCALE * 2)

// ================= 配置区域 =================
#define MXL_STARTUP_DELAY 1500
#define TA_SHIFT 8 //Default shift for MLX90640 in open air
const byte MLX90640_address = 0x33;

// 参数结构体（静态分配在 DRAM，Core 0 运行时需要频繁访问）
static paramsMLX90640 mlx90640;
static paramsMLX90641 mlx90641;

// 帧数据缓冲区指针（动态分配在 DRAM，连接成功后分配）
static uint16_t* mlx90640Frame = nullptr;
static float* internal_calc_buffer = nullptr;

static bool is_90640 = true; // 是否为 MLX90640 传感器


void probe_loop_mlx();
inline unsigned short CelsiusToDeciKelvin(float temp_c) {
    float k = temp_c + 273.15f;
    if (k < 0) k = 0; // 防止负数变成极是大整数
    return (unsigned short)(k * 10.0f + 0.5f);
}

/**
 * @brief 为 MLX90640/90641 动态申请内存
 * 大小固定为 834 个 float (约 3.3KB)，足够容纳 90640 的 768 个点以及潜在的溢出缓冲区
 */
void alloc_mlx_memory() {
    if (mlxBufferA != nullptr) return;

    // 分配两个 float 缓冲区 (双缓冲)
    size_t size_float = 834 * sizeof(float);
    size_t total_bytes = size_float * 2 + 834 * sizeof(uint16_t); // Buffer A + Buffer B + IntBuffer
    
    Serial.printf(">> MLX Memory: Allocating %d Bytes... ", (int)total_bytes);

    mlxBufferA = new float[834];
    mlxBufferB = new float[834];
    mlx90640To_buffer = new uint16_t[834]; // 颜色映射缓存

    if (mlxBufferA && mlxBufferB && mlx90640To_buffer) {
        memset(mlxBufferA, 0, size_float);
        memset(mlxBufferB, 0, size_float);
        memset(mlx90640To_buffer, 0, 834 * sizeof(uint16_t));

        // 初始化指针
        pWriteBuffer = mlxBufferA;
        pReadBuffer  = mlxBufferB;
        
        Serial.println("SUCCESS (Double Buffer Ready)");
        if (swapMutex == NULL) {
            swapMutex = xSemaphoreCreateMutex();
        }
        // 分配插值查找表（在 PSRAM 中）
        alloc_interp_tables();
        
        // 分配帧数据缓冲区（在 DRAM 中）
        mlx90640Frame = (uint16_t*)heap_caps_malloc(834 * sizeof(uint16_t), MALLOC_CAP_8BIT);
        internal_calc_buffer = (float*)heap_caps_malloc(834 * sizeof(float), MALLOC_CAP_8BIT);
        
        if (mlx90640Frame && internal_calc_buffer) {
            Serial.println(" >> Frame buffers allocated in DRAM.");
        } else {
            Serial.println(" >> Warning: Failed to allocate frame buffers in DRAM!");
        }
    } else {
        Serial.println("FAILED!");
    }
}

// 释放 MLX 相关内存（插值查找表和帧缓冲区）
inline void free_mlx_memory() {
    free_interp_tables();
    
    // 释放帧数据缓冲区
    if (mlx90640Frame != nullptr) {
        heap_caps_free(mlx90640Frame);
        mlx90640Frame = nullptr;
    }
    if (internal_calc_buffer != nullptr) {
        heap_caps_free(internal_calc_buffer);
        internal_calc_buffer = nullptr;
    }
}

// ================= 辅助函数 =================
inline uint16_t mlx_pixel_count(){ return is_90640 ? MLX90640_PIXELS : MLX90641_PIXELS; }
inline uint8_t mlx_cols(){ return is_90640 ? MLX90640_COLS : MLX90641_COLS; }
inline uint8_t mlx_rows(){ return is_90640 ? MLX90640_ROWS : MLX90641_ROWS; }
inline uint8_t mlx_scale(){ return is_90640 ? BASE_SCALE : (BASE_SCALE * 2); }

// 根据屏幕坐标获取 MLX 温度值 (处理坐标翻转)
float get_mlx_temperature(int screen_x, int screen_y) {
   if (pReadBuffer == nullptr) return 0.0f;

   int cols = mlx_cols();
   int rows = mlx_rows();
   int scale = (int)mlx_scale();
   
   if (screen_x < 0) screen_x = 0;
   if (screen_y < 0) screen_y = 0;
   
   int sensor_col = screen_x / scale;
   if (sensor_col >= cols) sensor_col = cols - 1;

   int sensor_row = rows - 1 - (screen_y / scale);
   if (sensor_row < 0) sensor_row = 0;
   if (sensor_row >= rows) sensor_row = rows - 1;

   int idx = sensor_row * cols + sensor_col;
   
   // 从读缓冲区读取
   return ((float*)pReadBuffer)[idx]; 
}

// ================= 核心功能实现 =================

/**
 * @brief 阻塞式初始化并深度检查 MLX 传感器
 * @param max_retries 最大重试次数。-1 为无限重试。
 * @return true: 初始化并自检成功; false: 失败
 */
bool blocking_mlx_init_and_check(int max_retries = -1) {
    int retry_count = 0;
    if (current_sensor == SENSOR_MLX90640) {is_90640 = true;}
    else if (current_sensor == SENSOR_MLX90641) {is_90640 = false;}
    else {
        Serial.println("Error: blocking_mlx_init_and_check called but current_sensor is not MLX.");
        return false;
    }
    pinMode(MLX_SDA, INPUT_PULLUP);
    pinMode(MLX_SCL, INPUT_PULLUP);
    
    probeWire->setPins(MLX_SDA, MLX_SCL);
    delay(MXL_STARTUP_DELAY); // 等待传感器上电稳定
    
    while (true) {
        retry_count++;
        Serial.printf("MLX Probe Attempt %d", retry_count);
        if (max_retries > 0) Serial.printf("/%d", max_retries);
        Serial.println("...");

        // ==========================================
        // 1. 硬件总线初始化 (MLX 需要高速 I2C)
        // ==========================================
        probeWire->begin();
        delay(5);
        probeWire->setClock(800000); // MLX90640 推荐 800kHz - 1MHz 
        delay(10); // 给一点总线稳定时间

        // ==========================================
        // 2. 第一步：基础 I2C 地址应答检查 (Ping)
        // ==========================================
        probeWire->beginTransmission((uint8_t)MLX90640_address);
        byte error = probeWire->endTransmission();
        
        bool step1_ok = (error == 0);
        bool step2_ok = false;
        bool step3_ok = false;

        if (step1_ok) {
             Serial.println(" >> I2C Address ACK: OK");
        } else {
             Serial.println(" >> I2C Address NACK (No Response)");
        }

        // ==========================================
        // 3. 第二步：读取状态/控制寄存器 (类似 ID 检查)
        // ==========================================
        if (step1_ok) {
            uint16_t controlRegister1 = 0;
            int status;
            
            if (current_sensor == SENSOR_MLX90640) {
                 status = MLX90640_I2CRead(MLX90640_address, 0x800D, 1, &controlRegister1);
            } else {
                 status = MLX90641_I2CRead(MLX90640_address, 0x800D, 1, &controlRegister1);
            }

            if (status == 0) {
                Serial.printf(" >> Control Reg (0x800D) Read: 0x%04X\n", controlRegister1);
                step2_ok = true;
            } else {
                Serial.printf(" >> Register Read Failed. Err: %d\n", status);
            }
        }

        // ==========================================
        // 4. 第三步：EEPROM 完整性与参数提取 (最严格的检查)
        // ==========================================
        if (step1_ok && step2_ok) {
            int status = -1;
            Serial.println(" >> Verifying EEPROM & Extracting Parameters...");

            // EEPROM 缓冲区放在 DRAM（静态分配，避免栈溢出）
            // static uint16_t eeMLX9064x[832];
            uint16_t* eeMLX9064x = (uint16_t*)malloc(832 * sizeof(uint16_t));
            if (eeMLX9064x == nullptr) {
                Serial.println(" >> Fatal Error: Not enough Heap memory for EEPROM!");
                return false; // 内存不足直接退出
            }
            // 结构体本身在 DRAM（静态分配），直接清零使用
            if (current_sensor == SENSOR_MLX90640) {
                memset(&mlx90640, 0, sizeof(mlx90640));
                int dumpStatus = MLX90640_DumpEE(MLX90640_address, eeMLX9064x);
                if (dumpStatus == 0) {
                    status = MLX90640_ExtractParameters(eeMLX9064x, &mlx90640);
                }
            } else {
                memset(&mlx90641, 0, sizeof(mlx90641));
                int dumpStatus = MLX90641_DumpEE(MLX90640_address, eeMLX9064x);
                if (dumpStatus == 0) {
                    status = MLX90641_ExtractParameters(eeMLX9064x, &mlx90641);
                }
            }
            free(eeMLX9064x);
            Serial.println(" >> Heap memory for EEPROM freed.");

            if (status == 0) {
                Serial.println(" >> Parameters Extracted: OK");
                step3_ok = true;
            } else {
                Serial.println(" >> EEPROM/Param Error: Data Corrupt.");
            }
        }

        // ==========================================
        // 5. 第四步：写入配置 (设置刷新率)
        // ==========================================
        if (step3_ok) {
            int setRateStatus;
            if (current_sensor == SENSOR_MLX90640) {
                // 0x04=8Hz, 0x05=16Hz, 0x06=32Hz, 0x07=64Hz
                setRateStatus = MLX90640_SetRefreshRate(MLX90640_address, 0x05); 
                MLX90640_SetResolution(MLX90640_address, 0x03); 
            } else {
                MLX90641_I2CWrite(0x33, 0x800D, 6401); 
                setRateStatus = MLX90641_SetRefreshRate(MLX90640_address, 0x05);
            }
            
            if (setRateStatus == 0) {
                alloc_mlx_memory();
                
                Serial.println(" >> Success: MLX Sensor Ready.");
                
                Serial.println(" >> Tring get one frame data from mlx...");
                probe_loop_mlx();
                Serial.println(" >> Success! start looping ...");
                return true; // === 成功返回 ===
            } else {
                 Serial.println(" >> Config Write Failed.");
            }
        }

        // ==========================================
        // 失败处理
        // ==========================================
        if (max_retries > 0 && retry_count >= max_retries) {
            Serial.println(" >> Maximum retries reached. Giving up.");
            free_mlx_memory(); // 清理已分配的内存
            return false;
        }

        Serial.println(" >> Retrying in 1s...");
        delay(1000);
    }
}


void probe_loop_mlx(){
    if (flag_in_photo_mode){return;}
    
    // 检查缓冲区是否已分配（在 alloc_mlx_memory 中分配）
    if (mlx90640Frame == nullptr || internal_calc_buffer == nullptr) return;
    
    static int status;
    static float vdd, Ta, tr, emissivity;
    // 获取当前双缓冲的写指针（这是给 UI 用的）
    float* currentWriteTarget = (float*)pWriteBuffer;
    if (currentWriteTarget == nullptr) return; 

    // ==========================================
    // 1. 读取数据并更新
    // ==========================================
    if (current_sensor == SENSOR_MLX90640){
        // 第一次
        status = MLX90640_GetFrameData(MLX90640_address, mlx90640Frame);
        
        vdd = MLX90640_GetVdd(mlx90640Frame, &mlx90640);
        Ta = MLX90640_GetTa(mlx90640Frame, &mlx90640);
        tr = Ta - TA_SHIFT;
        emissivity = 0.95;
        MLX90640_CalculateTo(mlx90640Frame, &mlx90640, emissivity, tr, internal_calc_buffer);
    } else {
        // MLX90641 同理
        status = MLX90641_GetFrameData(MLX90640_address, mlx90640Frame);
        vdd = MLX90641_GetVdd(mlx90640Frame, &mlx90641);
        Ta = MLX90641_GetTa(mlx90640Frame, &mlx90641);
        tr = Ta - TA_SHIFT;
        emissivity = 0.95;
        MLX90641_CalculateTo(mlx90640Frame, &mlx90641, emissivity, tr, internal_calc_buffer);
    }
    memcpy(currentWriteTarget, internal_calc_buffer, sizeof(float) * 834);
    // ==========================================
    // 3. 后处理 (Min/Max)
    // ==========================================
    uint16_t pix = mlx_pixel_count();
    uint8_t cols = mlx_cols();
    
    // 初始化统计变量
    float local_min = currentWriteTarget[0];
    float local_max = currentWriteTarget[0];
    double local_avg = 0;

    for (int i = 0; i < pix; i++){ // 从0开始遍历更安全
         // 简单的坏点过滤 (-41 ~ 301)
         if((currentWriteTarget[i] > -41) && (currentWriteTarget[i] < 301))
            {
               // 统计 Min
               if(currentWriteTarget[i] < local_min) {
                  local_min = currentWriteTarget[i];
               }

               // 统计 Max
               if(currentWriteTarget[i] > local_max) {
                  local_max = currentWriteTarget[i];
                  x_max = i / cols;
                  y_max = i % cols;
               }
            }
        else if(i > 0){
            currentWriteTarget[i] = currentWriteTarget[i-1]; // 简单补点
        }
        else {
            currentWriteTarget[i] = currentWriteTarget[i+1];
        }
        local_avg += currentWriteTarget[i];
    }

    // ==========================================
    // 4. 交换指针 (提交给 UI) 并更新温度统计变量
    // ==========================================
    if (xSemaphoreTake(swapMutex, pdMS_TO_TICKS(15)) == pdTRUE){
        volatile float* temp = pWriteBuffer;
        pWriteBuffer = pReadBuffer;
        pReadBuffer = temp;
        prob_lock = false;
        // 温度统计变量更新
        T_avg_fp = (float)(local_avg / pix);
        T_max_fp = local_max;
        T_min_fp = local_min;
        T_max = CelsiusToDeciKelvin(T_max_fp);
        T_min = CelsiusToDeciKelvin(T_min_fp);
        T_avg = CelsiusToDeciKelvin(T_avg_fp);
        hasNewData = true;
        xSemaphoreGive(swapMutex); // 释放锁

    }
}