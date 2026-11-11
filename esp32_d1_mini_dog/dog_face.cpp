#include "dog_face.h"
#include "config.h"

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

static Adafruit_SSD1306 _display(OLED_WIDTH, OLED_HEIGHT, &Wire, OLED_RESET);

bool DogFace::begin() {
  Wire.begin(OLED_SDA, OLED_SCL);
  Wire.setClock(400000);

  _ok = _display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR);
  if (!_ok) _ok = _display.begin(SSD1306_SWITCHCAPVCC, 0x3D);
  if (!_ok) return false;

  _display.clearDisplay();
  _display.setTextColor(SSD1306_WHITE);
  _display.setTextWrap(false);
  _display.display();

  _status = FW_NAME;
  _mood = MOOD_HAPPY;
  _moodStart = millis();
  _nextChange = millis() + 4000;
  render();
  return true;
}

void DogFace::update() {
  if (!_ok) return;

  uint32_t now = millis();
  if ((int32_t)(now - _nextChange) >= 0) pickRandomMood();
  if (now - _renderAt < 90) return;

  _renderAt = now;
  render();
}

void DogFace::setMood(DogMood mood, uint32_t holdMs) {
  uint32_t now = millis();
  if (_mood != mood) _moodStart = now;
  _mood = mood;
  _nextChange = now + (holdMs ? holdMs : 2500);
  _renderAt = 0;
}

void DogFace::setStatus(const String &text) {
  if (text == _status) return;
  _status = text;
  _renderAt = 0;
}

void DogFace::pickRandomMood() {
  static const DogMood kMoods[] = {
      MOOD_HAPPY, MOOD_HAPPY, MOOD_HAPPY,
      MOOD_BLINK, MOOD_BLINK,
      MOOD_SLEEP, MOOD_LOVE, MOOD_LOVE,
      MOOD_ANGRY, MOOD_DIZZY,
      MOOD_EXCITED, MOOD_EXCITED, MOOD_WALK};
  const long count = (long)(sizeof(kMoods) / sizeof(kMoods[0]));

  DogMood m = _mood;
  for (int i = 0; i < 6 && m == _mood; i++) {
    m = kMoods[random(count)];
  }

  uint32_t now = millis();
  _mood = m;
  _moodStart = now;
  if (m == MOOD_BLINK) {
    _nextChange = now + 380;
  } else if (m == MOOD_SLEEP) {
    _nextChange = now + random(5000, 9000);
  } else {
    _nextChange = now + random(3000, 6500);
  }
}

void DogFace::drawHeart(int x, int y, int r) {
  int h = r / 2;
  _display.fillCircle(x - h, y - h, h, SSD1306_WHITE);
  _display.fillCircle(x + h, y - h, h, SSD1306_WHITE);
  _display.fillTriangle(x - r, y - h + 1, x + r, y - h + 1, x, y + r, SSD1306_WHITE);
}

void DogFace::drawCurve(int cx, int cy, int w, int h, bool smile) {
  int prevX = cx - w / 2;
  int prevY = cy;
  for (int i = 1; i <= 16; i++) {
    float t = i / 16.0f;
    int x = cx - w / 2 + (int)(w * t);
    float k = 1.0f - powf(2.0f * t - 1.0f, 2.0f);
    int y = cy + (smile ? 1 : -1) * (int)(h * k);
    _display.drawLine(prevX, prevY, x, y, SSD1306_WHITE);
    prevX = x;
    prevY = y;
  }
}

void DogFace::drawEyes(uint32_t now, uint32_t age) {
  const int cx1 = 44, cx2 = 84, cy = 25;

  switch (_mood) {
    case MOOD_BLINK:
      _display.drawLine(cx1 - 7, cy, cx1 + 7, cy, SSD1306_WHITE);
      _display.drawLine(cx2 - 7, cy, cx2 + 7, cy, SSD1306_WHITE);
      break;

    case MOOD_SLEEP:
      _display.drawLine(cx1 - 7, cy - 3, cx1 - 3, cy + 3, SSD1306_WHITE);
      _display.drawLine(cx1 - 3, cy + 3, cx1 + 3, cy + 3, SSD1306_WHITE);
      _display.drawLine(cx1 + 3, cy + 3, cx1 + 7, cy - 3, SSD1306_WHITE);
      _display.drawLine(cx2 - 7, cy - 3, cx2 - 3, cy + 3, SSD1306_WHITE);
      _display.drawLine(cx2 - 3, cy + 3, cx2 + 3, cy + 3, SSD1306_WHITE);
      _display.drawLine(cx2 + 3, cy + 3, cx2 + 7, cy - 3, SSD1306_WHITE);
      {
        int stage = (age / 600) % 3;
        _display.setCursor(104 - stage * 5, 10 + stage * 4);
        _display.print("z");
      }
      break;

    case MOOD_LOVE:
      drawHeart(cx1, cy, 8);
      drawHeart(cx2, cy, 8);
      break;

    case MOOD_ANGRY:
      _display.drawLine(cx1 - 10, cy - 11, cx1 + 7, cy - 5, SSD1306_WHITE);
      _display.drawLine(cx2 + 10, cy - 11, cx2 - 7, cy - 5, SSD1306_WHITE);
      _display.fillRect(cx1 - 8, cy - 3, 16, 7, SSD1306_WHITE);
      _display.fillRect(cx2 - 8, cy - 3, 16, 7, SSD1306_WHITE);
      break;

    case MOOD_DIZZY: {
      double a = now * 0.006;
      _display.drawCircle(cx1, cy, 8, SSD1306_WHITE);
      _display.drawCircle(cx1, cy, 4, SSD1306_WHITE);
      _display.drawCircle(cx2, cy, 8, SSD1306_WHITE);
      _display.drawCircle(cx2, cy, 4, SSD1306_WHITE);
      _display.drawLine(cx1, cy, cx1 + (int)(cos(a) * 7), cy + (int)(sin(a) * 7), SSD1306_WHITE);
      _display.drawLine(cx2, cy, cx2 + (int)(cos(a + PI) * 7), cy + (int)(sin(a + PI) * 7), SSD1306_WHITE);
      break;
    }

    case MOOD_EXCITED:
      _display.fillCircle(cx1, cy, 8, SSD1306_WHITE);
      _display.fillCircle(cx2, cy, 8, SSD1306_WHITE);
      _display.drawLine(cx1 - 12, cy - 12, cx1 - 12, cy - 7, SSD1306_WHITE);
      _display.drawLine(cx2 + 12, cy - 12, cx2 + 12, cy - 7, SSD1306_WHITE);
      break;

    case MOOD_WALK: {
      int dx = (int)(sinf(now * 0.008f) * 4.0f);
      _display.fillRoundRect(cx1 - 6 + dx, cy - 6, 12, 12, 6, SSD1306_WHITE);
      _display.fillRoundRect(cx2 - 6 + dx, cy - 6, 12, 12, 6, SSD1306_WHITE);
      break;
    }

    default:
      _display.fillRoundRect(cx1 - 6, cy - 6, 12, 12, 6, SSD1306_WHITE);
      _display.fillRoundRect(cx2 - 6, cy - 6, 12, 12, 6, SSD1306_WHITE);
      break;
  }
}

void DogFace::render() {
  if (!_ok) return;

  uint32_t now = millis();
  uint32_t age = now - _moodStart;

  _display.clearDisplay();
  _display.setTextColor(SSD1306_WHITE);
  _display.setTextSize(1);
  _display.setTextWrap(false);

  // 耳朵
  _display.fillTriangle(14, 10, 42, 2, 34, 26, SSD1306_WHITE);
  _display.fillTriangle(114, 10, 86, 2, 94, 26, SSD1306_WHITE);

  // 頭型
  _display.drawRoundRect(18, 6, 92, 46, 14, SSD1306_WHITE);

  drawEyes(now, age);

  // 鼻子
  _display.fillRoundRect(60, 35, 8, 5, 2, SSD1306_WHITE);

  // 嘴巴
  switch (_mood) {
    case MOOD_SLEEP:
      drawCurve(64, 47, 18, 4, true);
      _display.fillCircle(64, 47, 2, SSD1306_WHITE);
      break;
    case MOOD_DIZZY:
      drawCurve(64, 46, 24, 4, false);
      break;
    case MOOD_ANGRY:
      drawCurve(64, 47, 22, 5, false);
      break;
    case MOOD_EXCITED:
      _display.fillRoundRect(55, 40, 18, 11, 5, SSD1306_WHITE);
      break;
    case MOOD_LOVE:
      drawCurve(64, 43, 22, 5, true);
      _display.fillCircle(64, 49, 3, SSD1306_WHITE);
      break;
    default:
      drawCurve(64, 43, 22, 5, true);
      _display.fillRect(61, 48, 6, 4, SSD1306_WHITE);
      break;
  }

  // 狀態列
  _display.drawFastHLine(0, 54, OLED_WIDTH, SSD1306_WHITE);
  if (_status.length() > 0) {
    String s = _status;
    if (s.length() > 21) s = s.substring(0, 21);
    int16_t x = (OLED_WIDTH - (int16_t)(s.length() * 6)) / 2;
    if (x < 0) x = 0;
    _display.setCursor(x, 56);
    _display.print(s);
  }

  _display.display();
}
