#pragma once

#include <Arduino.h>
#include <WiFi.h>

/**
 * @brief 初始化网络模块，默认设置为 STA 模式
 */
inline void network_init() {
    WiFi.mode(WIFI_STA);
    WiFi.disconnect(true); // 清除旧的连接配置
    Serial.println("[Network] WiFi STA mode initialized.");
}

/**
 * @brief 打印当前 WiFi 连接状态
 */
inline void nmcli_status() {
    Serial.println("=== Network Status ===");
    if (WiFi.status() == WL_CONNECTED) {
        Serial.printf("State: CONNECTED\nSSID: %s\nIP: %s\nRSSI: %d dBm\nMAC: %s\n",
            WiFi.SSID().c_str(),
            WiFi.localIP().toString().c_str(),
            WiFi.RSSI(),
            WiFi.macAddress().c_str());
    } else {
        Serial.println("State: DISCONNECTED");
    }
    Serial.println("======================");
}

/**
 * @brief 扫描附近的 WiFi 网络并打印结果
 */
inline void nmcli_scan() {
    Serial.println("Scanning WiFi...");
    int n = WiFi.scanNetworks();
    if (n == 0) {
        Serial.println("No networks found.");
    } else {
        Serial.printf("%-4s %-24s %-8s %-4s\n", "No.", "SSID", "RSSI", "CH");
        for (int i = 0; i < n; ++i) {
            Serial.printf("%-4d %-24s %-8d %-4d\n", i + 1,
                WiFi.SSID(i).c_str(),
                WiFi.RSSI(i),
                WiFi.channel(i));
        }
    }
    WiFi.scanDelete();
}

/**
 * @brief 连接到指定的 WiFi 网络
 * @param ssid 网络名称
 * @param pwd  密码
 */
inline void nmcli_connect(const String& ssid, const String& pwd) {
    Serial.printf("Connecting to \"%s\" ...\n", ssid.c_str());
    WiFi.begin(ssid.c_str(), pwd.c_str());
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 30) {
        delay(500);
        Serial.print(".");
        attempts++;
    }
    Serial.println();
    if (WiFi.status() == WL_CONNECTED) {
        Serial.printf("Connected! IP: %s\n", WiFi.localIP().toString().c_str());
    } else {
        Serial.println("Connection failed.");
    }
}

/**
 * @brief nmcli 命令入口
 *        支持:
 *        - nmcli                          查询连接状态
 *        - nmcli dev wifi                 扫描 WiFi
 *        - nmcli dev wifi connect <SSID> password <PWD>  连接 WiFi
 * @param command 串口输入的完整命令字符串
 */
inline void nmcli(String command) {
    command.trim();
    if (command.equalsIgnoreCase("nmcli")) {
        nmcli_status();
    } else if (command.equalsIgnoreCase("nmcli dev wifi")) {
        nmcli_scan();
    } else if (command.startsWith("nmcli dev wifi connect ")) {
        String rest = command.substring(String("nmcli dev wifi connect ").length());
        int pwIdx = rest.indexOf(" password ");
        if (pwIdx == -1) {
            Serial.println("[Error] Usage: nmcli dev wifi connect <SSID> password <PWD>");
            return;
        }
        String ssid = rest.substring(0, pwIdx);
        String pwd  = rest.substring(pwIdx + String(" password ").length());
        nmcli_connect(ssid, pwd);
    } else {
        Serial.println("[Error] Unknown nmcli command.");
        Serial.println("Supported commands:");
        Serial.println("  nmcli");
        Serial.println("  nmcli dev wifi");
        Serial.println("  nmcli dev wifi connect <SSID> password <PWD>");
    }
}
