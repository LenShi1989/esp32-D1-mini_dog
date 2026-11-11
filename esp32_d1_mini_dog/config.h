#pragma once

// ---------------- 韌體資訊 ----------------
#define FW_NAME    "DogBot"
#define FW_VERSION "1.0.0"

// ---------------- OLED SSD1306 (I2C) ----------------
#define OLED_SDA    21
#define OLED_SCL    22
#define OLED_WIDTH  128
#define OLED_HEIGHT 64
#define OLED_RESET  -1
#define OLED_ADDR   0x3C

// ---------------- SG90 伺服馬達 ----------------
// 編號：0=前左(M1) 1=前右(M2) 2=後左(M3) 3=後右(M4)
#define SERVO_PIN_FL 25
#define SERVO_PIN_FR 26
#define SERVO_PIN_RL 27
#define SERVO_PIN_RR 14
#define SERVO_MIN_US 500
#define SERVO_MAX_US 2400
#define SERVO_CENTER 90

// ---------------- WiFi ----------------
#define WIFI_CONFIG_FILE        "/wifi.txt"
#define WIFI_CONNECT_TIMEOUT_MS 15000
#define WIFI_RETRY_AP_MS        20000
#define AP_PASSWORD             "12345678"

// ---------------- 伺服馬達校正 ----------------
#define SERVO_CONFIG_FILE "/servo.cfg"

// ---------------- 板載狀態燈 ----------------
#define STATUS_LED 2
