#include "dog_servo.h"
#include "config.h"

// ESP32 core 2.x 使用 ledcSetup + ledcAttachPin（以 channel 控制）
// ESP32 core 3.x 使用 ledcAttach + ledcWrite（以 pin 控制）
#ifndef ESP_ARDUINO_VERSION_MAJOR
#define ESP_ARDUINO_VERSION_MAJOR 2
#endif

namespace {

// 0=FL 1=FR 2=RL 3=RR
const uint8_t LEG_PINS[4] = {SERVO_PIN_FL, SERVO_PIN_FR, SERVO_PIN_RL, SERVO_PIN_RR};
const bool    LEFT_LEG[4] = {true, false, true, false};
const bool    DIAG_A[4]   = {true, false, false, true};  // 對角步態：FL + RR 同相

// 姿勢角度（可依實際機構調整）
const float POSE_STAND[4] = {90, 90, 90, 90};
const float POSE_SIT[4]   = {112, 112, 68, 68};
const float POSE_LIE[4]   = {136, 136, 44, 44};

inline float clampf(float v, float lo, float hi) {
  return v < lo ? lo : (v > hi ? hi : v);
}

void servoAttach(uint8_t index, uint8_t pin) {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  (void)index;
  ledcAttach(pin, 50, 16);
#else
  (void)pin;
  ledcSetup(index, 50, 16);
  ledcAttachPin(LEG_PINS[index], index);
#endif
}

void servoWrite(uint8_t index, uint8_t pin, uint16_t duty) {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  (void)index;
  ledcWrite(pin, duty);
#else
  (void)pin;
  ledcWrite(index, duty);
#endif
}

uint16_t angleToDuty(float angle) {
  angle = clampf(angle, 0.0f, 180.0f);
  float pulse = SERVO_MIN_US + (angle / 180.0f) * (float)(SERVO_MAX_US - SERVO_MIN_US);
  return (uint16_t)(pulse * 65535.0f / 20000.0f + 0.5f);
}

}  // namespace

void DogServo::begin() {
  for (uint8_t i = 0; i < 4; i++) {
    servoAttach(i, LEG_PINS[i]);
    servoWrite(i, LEG_PINS[i], angleToDuty(SERVO_CENTER));
    _current[i] = SERVO_CENTER;
    _target[i]  = SERVO_CENTER;
    _written[i] = SERVO_CENTER;
  }
  _mode = MODE_STAND;
  _lastUpdate = millis();
}

void DogServo::update() {
  uint32_t now = millis();
  uint32_t elapsed = now - _lastUpdate;
  if (elapsed < 8) return;
  _lastUpdate = now;

  float dt = elapsed / 1000.0f;
  if (dt > 0.12f) dt = 0.12f;

  // 舉手／撒嬌時間到後自動回待機
  if ((_mode == MODE_WAVE || _mode == MODE_WAG) &&
      _holdUntil && (int32_t)(now - _holdUntil) >= 0) {
    _mode = MODE_STAND;
    _holdUntil = 0;
  }

  switch (_mode) {
    case MODE_WALK: {
      float mag  = fmaxf(fabsf(_forward), fabsf(_turn));
      float freq = 1.3f + 1.7f * mag;              // 0.9 ~ 3 Hz
      float dir  = (_forward < -0.05f) ? -1.0f : 1.0f;
      _phase += 2.0f * PI * freq * dir * dt;
      while (_phase >= 2.0f * PI) _phase -= 2.0f * PI;
      while (_phase < 0.0f)       _phase += 2.0f * PI;

      float swing = 24.0f * fabsf(_forward) + 13.0f * fabsf(_turn);
      for (uint8_t i = 0; i < 4; i++) {
        float ph   = _phase + (DIAG_A[i] ? 0.0f : PI);
        float bias = _turn * (LEFT_LEG[i] ? 18.0f : -18.0f);
        _target[i] = SERVO_CENTER + sinf(ph) * swing + bias;
      }
      break;
    }

    case MODE_WAVE: {
      float w = sinf(now * 0.012f) * 22.0f;
      _target[0] = SERVO_CENTER;
      _target[2] = SERVO_CENTER;
      _target[3] = SERVO_CENTER;
      _target[1] = 122.0f + w;                     // 前右腳舉起揮動
      break;
    }

    case MODE_WAG: {
      float w = sinf(now * 0.014f);
      _target[0] = SERVO_CENTER + 8.0f * w;
      _target[1] = SERVO_CENTER - 8.0f * w;
      _target[2] = SERVO_CENTER - 14.0f * w;
      _target[3] = SERVO_CENTER + 14.0f * w;
      break;
    }

    case MODE_SIT:
      for (uint8_t i = 0; i < 4; i++) _target[i] = POSE_SIT[i];
      break;

    case MODE_LIE:
      for (uint8_t i = 0; i < 4; i++) _target[i] = POSE_LIE[i];
      break;

    case MODE_MANUAL:
      // 由 setManual() / centerAll() 直接指定 _target
      break;

    default:
      for (uint8_t i = 0; i < 4; i++) _target[i] = POSE_STAND[i];
      break;
  }

  // 限速平滑移動，避免 SG90 瞬間跳動
  float maxStep = 220.0f * dt;
  for (uint8_t i = 0; i < 4; i++) {
    _target[i] = clampf(_target[i], 0.0f, 180.0f);
    float diff = _target[i] - _current[i];
    if (fabsf(diff) <= maxStep) {
      _current[i] = _target[i];
    } else {
      _current[i] += (diff > 0.0f) ? maxStep : -maxStep;
    }

    int value = (int)(_current[i] + 0.5f);
    if (value != _written[i]) {
      // 套用校正：以 90° 為中心反轉，再加上微調
      float physical = 90.0f + ((float)value - 90.0f) * (_rev[i] ? -1.0f : 1.0f) + (float)_trim[i];
      servoWrite(i, LEG_PINS[i], angleToDuty(physical));
      _written[i] = value;
    }
  }
}

void DogServo::setWalk(float forward, float turn) {
  _forward = clampf(forward, -1.0f, 1.0f);
  _turn    = clampf(turn, -1.0f, 1.0f);

  if (fabsf(_forward) < 0.08f && fabsf(_turn) < 0.08f) {
    _forward = 0.0f;
    _turn = 0.0f;
    if (_mode == MODE_WALK) _mode = MODE_STAND;
  } else {
    _mode = MODE_WALK;
    _holdUntil = 0;
  }
}

bool DogServo::action(const String &name) {
  String a = name;
  a.trim();
  a.toLowerCase();

  _forward = 0.0f;
  _turn = 0.0f;
  _holdUntil = 0;

  if (a == "stop" || a == "stand") {
    _mode = MODE_STAND;
  } else if (a == "sit") {
    _mode = MODE_SIT;
  } else if (a == "lie" || a == "down") {
    _mode = MODE_LIE;
  } else if (a == "wave" || a == "hand") {
    _mode = MODE_WAVE;
    _holdUntil = millis() + 3000;
  } else if (a == "wag" || a == "cute") {
    _mode = MODE_WAG;
    _holdUntil = millis() + 4000;
  } else {
    return false;
  }
  return true;
}

void DogServo::setManual(uint8_t channel, int angle) {
  if (channel > 3) return;
  _mode = MODE_MANUAL;
  _holdUntil = 0;
  _forward = 0.0f;
  _turn = 0.0f;
  _target[channel] = clampf((float)angle, 0.0f, 180.0f);
}

void DogServo::centerAll() {
  _mode = MODE_MANUAL;
  _holdUntil = 0;
  _forward = 0.0f;
  _turn = 0.0f;
  for (uint8_t i = 0; i < 4; i++) _target[i] = SERVO_CENTER;
}

void DogServo::setCalibration(uint8_t channel, int trim, bool reverse) {
  if (channel > 3) return;
  _trim[channel] = constrain(trim, -45, 45);
  _rev[channel] = reverse;
  _written[channel] = -1;  // 強制下一次 update 重新輸出
}

int DogServo::trim(uint8_t channel) const {
  return channel > 3 ? 0 : _trim[channel];
}

bool DogServo::reversed(uint8_t channel) const {
  return channel > 3 ? false : _rev[channel];
}

int DogServo::angle(uint8_t channel) const {
  if (channel > 3) return SERVO_CENTER;
  return (int)(_current[channel] + 0.5f);
}

const char *DogServo::modeName() const {
  switch (_mode) {
    case MODE_WALK:   return "walk";
    case MODE_SIT:    return "sit";
    case MODE_LIE:    return "lie";
    case MODE_WAVE:   return "wave";
    case MODE_WAG:    return "wag";
    case MODE_MANUAL: return "manual";
    default:          return "stand";
  }
}
