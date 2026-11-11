#pragma once

#include <Arduino.h>

enum DogMood : uint8_t {
  MOOD_HAPPY = 0,
  MOOD_BLINK,
  MOOD_SLEEP,
  MOOD_LOVE,
  MOOD_ANGRY,
  MOOD_DIZZY,
  MOOD_EXCITED,
  MOOD_WALK
};

// SSD1306 表情顯示
class DogFace {
 public:
  bool begin();
  void update();

  // holdMs = 0 表示交給隨機表情計時器接手
  void setMood(DogMood mood, uint32_t holdMs = 0);
  void setStatus(const String &text);

  bool available() const { return _ok; }

 private:
  void pickRandomMood();
  void render();
  void drawEyes(uint32_t now, uint32_t age);
  void drawHeart(int x, int y, int r);
  void drawCurve(int cx, int cy, int w, int h, bool smile);

  bool     _ok = false;
  DogMood  _mood = MOOD_HAPPY;
  uint32_t _moodStart = 0;
  uint32_t _nextChange = 0;
  uint32_t _renderAt = 0;
  String   _status;
};
