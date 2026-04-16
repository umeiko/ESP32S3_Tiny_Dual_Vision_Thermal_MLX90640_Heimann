#pragma once

#include <Arduino.h>
#include <LittleFS.h>
#include <ESPAsyncWebServer.h>
#include <esp_camera.h>
#include <esp_heap_caps.h>
#include "shared_val.h"
#include "mlx_drivers/mlx_probe.hpp"

AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

inline void onWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client,
                      AwsEventType type, void *arg, uint8_t *data, size_t len) {
    if (type == WS_EVT_CONNECT) {
        Serial.printf("[WS] Client %u connected from %s\n", client->id(), client->remoteIP().toString().c_str());
    } else if (type == WS_EVT_DISCONNECT) {
        Serial.printf("[WS] Client %u disconnected\n", client->id());
    }
}

/**
 * @brief 初始化 WebServer 与 LittleFS，注册 HTTP/WebSocket 路由
 *        数据流协议（二进制 WebSocket）:
 *        - 首字节 0x01 : 后接 JPEG 图像数据（摄像头）
 *        - 首字节 0x02 : 后接热成像数据 [w(1), h(1), tmin(4), tmax(4), pixels...]
 */
inline void ws_init() {
    if (!LittleFS.begin(true)) {
        Serial.println("[WebServer] LittleFS mount failed");
        return;
    }
    Serial.println("[WebServer] LittleFS mounted");

    ws.onEvent(onWsEvent);
    server.addHandler(&ws);

    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (LittleFS.exists("/index.html")) {
            request->send(LittleFS, "/index.html", "text/html");
        } else {
            request->send(200, "text/html",
                "<html><body><h1>index.html not found in LittleFS</h1>"
                "<p>Please build and upload the filesystem image.</p></body></html>");
        }
    });

    server.onNotFound([](AsyncWebServerRequest *request) {
        request->send(404, "text/plain", "Not found");
    });

    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Origin", "*");
    server.begin();
    Serial.println("[WebServer] HTTP server started on port 80");
}

/**
 * @brief WebSocket 推流循环，需在主 loop() 中调用
 *        有客户端连接时才采集并推送数据
 */
inline void ws_loop() {
    ws.cleanupClients();

    static unsigned long last_cam = 0;
    static unsigned long last_thermal = 0;
    const unsigned long cam_interval = 100;      // 摄像头 10 fps
    const unsigned long thermal_interval = 100;  // 热成像 10 fps

    if (ws.count() == 0) return;

    unsigned long now = millis();

    // ---- 摄像头 JPEG 流 (type 0x01) ----
    if (now - last_cam >= cam_interval) {
        last_cam = now;
        camera_fb_t *fb = esp_camera_fb_get();
        if (fb && fb->format == PIXFORMAT_JPEG) {
            size_t pkt_len = 1 + fb->len;
            uint8_t *pkt = (uint8_t *)heap_caps_malloc(pkt_len, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
            if (!pkt) pkt = (uint8_t *)malloc(pkt_len);
            if (pkt) {
                pkt[0] = 0x01;
                memcpy(pkt + 1, fb->buf, fb->len);
                ws.binaryAll(pkt, pkt_len);
                free(pkt);
            }
            esp_camera_fb_return(fb);
        } else {
            if (fb) esp_camera_fb_return(fb);
        }
    }

    // ---- 热成像数据流 (type 0x02) ----
    if (now - last_thermal >= thermal_interval) {
        last_thermal = now;
        uint8_t cols = mlx_cols();
        uint8_t rows = mlx_rows();
        uint16_t pix = mlx_pixel_count();
        size_t pkt_len = 1 + 2 + 4 + 4 + pix; // type + w/h + tmin + tmax + pixels
        uint8_t *pkt = (uint8_t *)malloc(pkt_len);
        if (pkt) {
            pkt[0] = 0x02;
            pkt[1] = cols;
            pkt[2] = rows;
            float tmin = 0.0f, tmax = 0.0f;
            bool got = false;
            if (mlx90640To_buffer != nullptr && swapMutex != nullptr &&
                xSemaphoreTake(swapMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
                tmin = T_min_fp;
                tmax = T_max_fp;
                uint16_t *src = (uint16_t *)mlx90640To_buffer;
                for (int i = 0; i < pix; i++) {
                    pkt[11 + i] = (uint8_t)src[i];
                }
                xSemaphoreGive(swapMutex);
                got = true;
            }
            if (got) {
                memcpy(pkt + 3, &tmin, sizeof(float));
                memcpy(pkt + 7, &tmax, sizeof(float));
                ws.binaryAll(pkt, pkt_len);
            }
            free(pkt);
        }
    }
}
