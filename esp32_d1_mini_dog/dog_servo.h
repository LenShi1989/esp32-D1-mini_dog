#pragma once

#include <Arduino.h>

#include "config.h"

// 機械狗動作模式
enum DogMode : uint8_t {
  MODE_STAND = 0,
  MODE_WALK,
  MODE_SIT,
  MODE_LIE,
  MODE_WAVE,
  MODE_WAG,
  MODE_MANUAL
};

// 四足伺服馬達控制（非阻塞步態引擎）
// 直接使用 ESP32 LEDC 產生 50Hz PWM，不依賴外部伺服馬達函式庫。
class DogServo {
 public:
  void begin();

  // 每 10ms 呼叫一次；內部會用實際經過時間平滑移動
  void update();

  // 行走：forward(-1 後退 ~ 1 前進)、turn(-1 左轉 ~ 1 右轉)
  void setWalk(float forward, float turn);

  // 特殊指令：stand / sit / lie / wave / wag / stop
  bool action(const String &name);

  // 手動測試：channel 0~3、angle 0~180
  void setManual(uint8_t channel, int angle);
  void centerAll();

  // 校正：trim 微調（度）、reverse 反轉安裝方向
  void setCalibration(uint8_t channel, int trim, bool reverse);
  int  trim(uint8_t channel) const;
  bool reversed(uint8_t channel) const;

  int  angle(uint8_t channel) const;
  DogMode mode() const { return _mode; }
  const char *modeName() const;

 private:
  float _current[4] = {SERVO_CENTER, SERVO_CENTER, SERVO_CENTER, SERVO_CENTER};
  float _target[4]  = {SERVO_CENTER, SERVO_CENTER, SERVO_CENTER, SERVO_CENTER};
  int   _written[4] = {-1, -1, -1, -1};
  int   _trim[4] = {0, 0, 0, 0};
  bool  _rev[4] = {false, false, false, false};

  DogMode _mode = MODE_STAND;
  float   _forward = 0.0f;
  float   _turn = 0.0f;
  float   _phase = 0.0f;

  uint32_t _lastUpdate = 0;
  uint32_t _holdUntil = 0;  // wave / wag 自動返回待機的時間
};
