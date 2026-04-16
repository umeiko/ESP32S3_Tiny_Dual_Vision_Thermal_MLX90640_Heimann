#ifndef _CST816T_H
#define _CST816T_H

#include <stdint.h>
#include <stdbool.h>
#include <Arduino.h>
#include <Wire.h>
#include "shared_val.h"

// 引脚定义：触摸
uint8_t TOUCH_SDA = 10;
uint8_t TOUCH_SCL = 11;
#define PinNotUsed 254
#define TOUCH_RST PinNotUsed
#define ROTATE 1
#define TOUCH_LONG_PUSH_T 200

#define TouchWidth 250
#define TouchHeight 300

#define I2C_ADDR_CST816T 0x15

// 触摸旋转方向
typedef enum {
    Rotation_0 = 0, 
    Rotation_1, 
    Rotation_2,
    Rotation_3, 
} TouchRotation; 

typedef enum :uint8_t {
    None = 0x00,
    SlideDown = 0x01,
    SlideUp = 0x02,
    SlideLeft = 0x03,
    SlideRight = 0x04,
    SingleTap = 0x05,
    DoubleTap = 0x0B,
    LongPress = 0x0C
}Gestures;

typedef struct{
    uint16_t x = 0;
    uint16_t y = 0;
    Gestures gesture = Gestures::None;
    bool touching = false;
    bool isValid = false;
}TouchInfos;

struct data_struct {
    uint16_t x = 0;
    uint16_t y = 0;
    Gestures gesture = Gestures::None;
    bool touching = false;
    bool isValid = false;
};


/**************************************************************************/
/*!
    @brief  CST816T I2C CTP controller driver
*/
/**************************************************************************/
class CST816T 
{
public:
    CST816T();
    CST816T(uint8_t rst_n, uint8_t int_n);
    CST816T(uint8_t sda, uint8_t scl, uint8_t rst_n, uint8_t int_n); 
    data_struct tp;
    virtual ~CST816T(); 

    void begin(void);
    void begin(u_int8_t sda, uint8_t scl);
    void setRotation(TouchRotation Rotation);
    int update();

    // Scan Function
    TouchInfos GetTouchInfo(void);

private: 
    int sda = -1; 
    int scl = -1; 
    uint8_t int_n = -1; 
    uint8_t rst_n = -1; 
    uint8_t touch_rotation = Rotation_0;

    static constexpr uint8_t gestureIndex = 1;
    static constexpr uint8_t touchPointNumIndex = 2;
    static constexpr uint8_t touchXHighIndex = 3;
    static constexpr uint8_t touchXLowIndex = 4;
    static constexpr uint8_t touchYHighIndex = 5;
    static constexpr uint8_t touchYLowIndex = 6;

    uint8_t readByte(uint8_t addr); 
    void writeByte(uint8_t addr, uint8_t data); 
}; 

CST816T::CST816T()
{
}

CST816T::CST816T(uint8_t rst_n_pin, uint8_t int_n_pin)
{
    rst_n = rst_n_pin;
    int_n = int_n_pin;
}

CST816T::CST816T(uint8_t sda_pin, uint8_t scl_pin, uint8_t rst_n_pin, uint8_t int_n_pin)
{
    sda = sda_pin;
    scl = scl_pin;
    rst_n = rst_n_pin;
    int_n = int_n_pin;
}

CST816T::~CST816T() {
}

void CST816T::begin(void) {
    // Initialize I2C
    if(sda != -1 && scl != -1) {
        Serial.printf("I2C initializing (SDA=%d, SCL=%d)\n", sda, scl);
        touchWire->begin(sda, scl); 
    }
    else {
        Serial.printf("I2C initializing with default pins");
        touchWire->begin(); 
    }

    // Int Pin Configuration
    if(int_n != -1) {
        pinMode(int_n, INPUT); 
    }
    // Reset Pin Configuration
    if(rst_n != -1) {
        pinMode(rst_n, OUTPUT); 
        digitalWrite(rst_n, LOW); 
        delay(10); 
        digitalWrite(rst_n, HIGH); 
        delay(500); 
    }
    readByte(0x15);
    delay(1);
    readByte(0xa7);
    delay(1);
    writeByte(0xEC, 0b00000101);
    delay(1);
    writeByte(0xFA, 0b01110000);
    delay(1);
}

void CST816T::begin(u_int8_t sda_pin, u_int8_t scl_pin) {
    this->sda = sda_pin;
    this->scl = scl_pin;
    this->begin();
}

/**
 * @设置旋转方向,默认为Rotation_0
 * @Rotation：方向 0~3
*/
void CST816T::setRotation(TouchRotation Rotation)
{
    switch (Rotation) {
    case Rotation_0:
      touch_rotation = Rotation_0;
      break;
    case Rotation_1:
      touch_rotation = Rotation_1;
      break;
    case Rotation_2:
      touch_rotation = Rotation_2;
      break;
    case Rotation_3:
      touch_rotation = Rotation_3;
      break;
    }
}

//coordinate diagram（FPC downwards）
TouchInfos CST816T::GetTouchInfo(void){
    byte error;
    TouchInfos info;
    uint8_t touchData[7];
    uint8_t rdDataCount;
    uint8_t i = 0;
    long startTime = millis();
    do {
        touchWire->beginTransmission(I2C_ADDR_CST816T); 
        touchWire->write(0); 
        error = touchWire->endTransmission(false); // Restart
        if (error != 0) {
            info.isValid = false;
            return info;
        }
        rdDataCount = touchWire->requestFrom(I2C_ADDR_CST816T, sizeof(touchData)); 
        if(millis() - startTime > 1) {
            info.isValid = false;
            return info;
        }
    } while(rdDataCount == 0); 
    i = 0;
    while(touchWire->available()) {
        touchData[i] = touchWire->read();
        i++;
        if(i >= sizeof(touchData)) {
            break;
        }
    }

    uint8_t nbTouchPoints = touchData[touchPointNumIndex] & 0x0f;
    uint8_t xHigh = touchData[touchXHighIndex] & 0x0f;
    uint8_t xLow = touchData[touchXLowIndex];
    uint16_t x = (xHigh << 8) | xLow;
    uint8_t yHigh = touchData[touchYHighIndex] & 0x0f;
    uint8_t yLow = touchData[touchYLowIndex];
    uint16_t y = (yHigh << 8) | yLow;
    Gestures gesture = static_cast<Gestures>(touchData[gestureIndex]);

    // Validity check
    if(x >= TouchWidth || y >= TouchHeight ||
        (gesture != Gestures::None &&
        gesture != Gestures::SlideDown &&
        gesture != Gestures::SlideUp &&
        gesture != Gestures::SlideLeft &&
        gesture != Gestures::SlideRight &&
        gesture != Gestures::SingleTap &&
        gesture != Gestures::DoubleTap &&
        gesture != Gestures::LongPress)) {
        info.isValid = false;
        return info;
    }

    info.x = x;
    info.y = y;
    info.touching = (nbTouchPoints > 0);
    info.gesture = gesture;
    info.isValid = true;
    return info;
}

//coordinate diagram（FPC downwards）
int CST816T::update(void){
    static TouchInfos data = GetTouchInfo();
    data = GetTouchInfo();
    if (data.isValid){
        tp.x = data.x;
        tp.y = data.y;
        tp.touching = data.touching;
        tp.gesture = data.gesture;
    }
    return 0;
}

// Private Function
uint8_t CST816T::readByte(uint8_t addr) {
    uint8_t rdData; 
    uint8_t rdDataCount;
    uint8_t i;
    do {
        touchWire->beginTransmission(I2C_ADDR_CST816T); 
        touchWire->write(addr); 
        touchWire->endTransmission(false); // Restart
        rdDataCount = touchWire->requestFrom(I2C_ADDR_CST816T, 1);
        i ++; 
    } while(rdDataCount == 0 && i<250); 
    
    while(touchWire->available()) {
        rdData = touchWire->read(); 
    }
    return rdData; 
}

void CST816T::writeByte(uint8_t addr, uint8_t data) {
    touchWire->beginTransmission(I2C_ADDR_CST816T); 
    touchWire->write(addr); 
    touchWire->write(data); 
    touchWire->endTransmission(); 
}

CST816T touch(TOUCH_SDA, TOUCH_SCL, TOUCH_RST, PinNotUsed);

void touch_setup(void){
    touch.begin();
}

void touch_loop(void){
    static bool last_touched = false;
    static uint8_t last_gesture = Gestures::None;
    static uint16_t last_x = 0, last_y = 0;

    touch.update();

    // 计算旋转后的坐标（ROTATE == 1）
    uint16_t x = touch.tp.y;
    uint16_t y = 240 - touch.tp.x;

    if(touch.tp.touching) {
        if(!last_touched || touch.tp.gesture != last_gesture || x != last_x || y != last_y) {
            Serial.printf("Touch: x=%d, y=%d, gesture=%d\n", x, y, touch.tp.gesture);
        }
    } else if(last_touched) {
        Serial.printf("Touch released: x=%d, y=%d\n", last_x, last_y);
    }

    last_touched = touch.tp.touching;
    last_gesture = touch.tp.gesture;
    last_x = x;
    last_y = y;
}

bool check_tp_i2c(){ 
    Serial.printf("Checking TP (sda %d, scl %d)\n", TOUCH_SDA, TOUCH_SCL);
    touchWire->begin(TOUCH_SDA, TOUCH_SCL);
    touchWire->beginTransmission(I2C_ADDR_CST816T);
    if (touchWire->endTransmission() == 0) {
        Serial.println("0x15 I2C communication OK");
        return true;
    } else {
        Serial.println("0x15 I2C communication failed");
        return false;
    }
}

void touch_task(void *ptr){
    for(;;){
        touch_loop();
    }
}

void touch_task_startup(){
    xTaskCreate(touch_task, "touch_task", 2048, NULL, 1, NULL);
}

#endif
