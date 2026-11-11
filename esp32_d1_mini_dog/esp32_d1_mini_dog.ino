/*
 * DogBot - ESP32 D1 mini 玩具狗
 * 版本：v1.1.0（2026-11-12）
 * Arduino IDE 1.8.19 + ESP32 core 2.x / 3.x
 *
 * 版本紀錄：
 *   v1.1.0  WiFi STA + AP 共存（AP 熱點常駐）
 *           AP SSID 改為 DogBot-<MAC 後 4 碼>，修正顯示 0000 的問題
 *           OTA 新增 SPIFFS 映像（spiffs.bin）上傳
 *           新增 tools/build_spiffs 建立映像指令稿
 *   v1.0.0  初版：四足步態、OLED 表情、SPIFFS 網頁控制台
 *
 * 前端網頁置於 data/，需先用 ESP32 Sketch Data Upload 上傳到 SPIFFS
 */

#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <SPIFFS.h>
#include <Update.h>
#include <esp_mac.h>
#include <esp_system.h>

#include "config.h"
#include "dog_servo.h"
#include "dog_face.h"

WebServer server(80);
DogServo dog;
DogFace face;

String wifiSsid;
String wifiPass;
String apSsid;

bool apMode = false;
bool restartPending = false;
uint32_t restartAt = 0;
uint32_t wifiLostAt = 0;

bool otaSuccess = false;
bool otaFsMode = false;
String otaMessage;

// ---------------------------------------------------------------------------
// 共用小工具
// ---------------------------------------------------------------------------

String jsonEscape(const String &s) {
  String out;
  out.reserve(s.length() + 8);
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    if (c == '"' || c == '\\') {
      out += '\\';
      out += c;
    } else if (c == '\n') {
      out += F("\\n");
    } else if (c == '\r') {
      out += F("\\r");
    } else if (c == '\t') {
      out += F("\\t");
    } else if ((uint8_t)c < 0x20) {
      out += ' ';
    } else {
      out += c;
    }
  }
  return out;
}

const char *mimeType(const String &path) {
  if (path.endsWith(".html")) return "text/html; charset=utf-8";
  if (path.endsWith(".css"))  return "text/css; charset=utf-8";
  if (path.endsWith(".js"))   return "application/javascript; charset=utf-8";
  if (path.endsWith(".json")) return "application/json; charset=utf-8";
  if (path.endsWith(".svg"))  return "image/svg+xml";
  if (path.endsWith(".png"))  return "image/png";
  if (path.endsWith(".ico"))  return "image/x-icon";
  return "text/plain; charset=utf-8";
}

// ---------------------------------------------------------------------------
// WiFi 設定（存於 SPIFFS /wifi.txt）
// ---------------------------------------------------------------------------

bool loadWifiConfig(String &ssid, String &pass) {
  if (!SPIFFS.exists(WIFI_CONFIG_FILE)) return false;

  File f = SPIFFS.open(WIFI_CONFIG_FILE, "r");
  if (!f) return false;

  while (f.available()) {
    String line = f.readStringUntil('\n');
    line.trim();
    int eq = line.indexOf('=');
    if (eq <= 0) continue;

    String key = line.substring(0, eq);
    String val = line.substring(eq + 1);
    if (key == "ssid") ssid = val;
    else if (key == "pass") pass = val;
  }
  f.close();
  return ssid.length() > 0;
}

bool saveWifiConfig(const String &ssid, const String &pass) {
  File f = SPIFFS.open(WIFI_CONFIG_FILE, "w");
  if (!f) return false;
  f.printf("ssid=%s\npass=%s\n", ssid.c_str(), pass.c_str());
  f.close();
  return true;
}

// ---------------------------------------------------------------------------
// 伺服馬達校正設定（存於 SPIFFS /servo.cfg）
// 格式：channel=trim,reverse
// ---------------------------------------------------------------------------

void loadServoConfig() {
  if (!SPIFFS.exists(SERVO_CONFIG_FILE)) return;

  File f = SPIFFS.open(SERVO_CONFIG_FILE, "r");
  if (!f) return;

  while (f.available()) {
    String line = f.readStringUntil('\n');
    line.trim();
    int eq = line.indexOf('=');
    if (eq <= 0) continue;

    int ch = line.substring(0, eq).toInt();
    if (ch < 0 || ch > 3) continue;

    String val = line.substring(eq + 1);
    int comma = val.indexOf(',');
    int trim = (comma >= 0) ? val.substring(0, comma).toInt() : val.toInt();
    bool rev = (comma >= 0) && (val.substring(comma + 1).toInt() != 0);

    dog.setCalibration((uint8_t)ch, trim, rev);
  }
  f.close();

  Serial.println("[Servo] Calibration loaded");
}

bool saveServoConfig() {
  File f = SPIFFS.open(SERVO_CONFIG_FILE, "w");
  if (!f) return false;

  for (uint8_t i = 0; i < 4; i++) {
    f.printf("%u=%d,%u\n", i, dog.trim(i), dog.reversed(i) ? 1 : 0);
  }
  f.close();
  return true;
}

// ---------------------------------------------------------------------------
// WiFi 連線 / AP 熱點（STA + AP 共存）
// ---------------------------------------------------------------------------
// AP SSID 後 4 碼：優先使用 WiFi STA MAC，必要時退回 efuse 基底位址
String macSuffix() {
  uint8_t mac[6] = {0, 0, 0, 0, 0, 0};
  esp_read_mac(mac, ESP_MAC_WIFI_STA);

  bool zero = true;
  for (uint8_t i = 0; i < 6; i++) {
    if (mac[i] != 0) {
      zero = false;
      break;
    }
  }

  char buf[8];
  if (zero) {
    snprintf(buf, sizeof(buf), "%04X", (uint16_t)(ESP.getEfuseMac() & 0xFFFF));
  } else {
    snprintf(buf, sizeof(buf), "%02X%02X", mac[4], mac[5]);
  }
  return String(buf);
}

void startAccessPoint() {
  if (apMode) return;

  // 切換到 AP_STA 不會中斷已連線的 STA
  if ((WiFi.getMode() & WIFI_AP) == 0) {
    WiFi.mode(WIFI_AP_STA);
  }

  apSsid = String(AP_SSID_PREFIX) + macSuffix();

  WiFi.softAPConfig(IPAddress(192, 168, 4, 1), IPAddress(192, 168, 4, 1),
                    IPAddress(255, 255, 255, 0));
  if (!WiFi.softAP(apSsid.c_str(), AP_PASSWORD)) {
    Serial.println("[WiFi] softAP failed");
    return;
  }

  apMode = true;
  Serial.printf("[WiFi] AP ready: %s / %s  http://%s\n",
                apSsid.c_str(), AP_PASSWORD, WiFi.softAPIP().toString().c_str());
  face.setStatus("AP " + WiFi.softAPIP().toString());
}

void beginSta() {
  if (wifiSsid.length() == 0) return;

  WiFi.setHostname("dogbot");
  WiFi.setSleep(false);
  WiFi.begin(wifiSsid.c_str(), wifiPass.c_str());
}

void maintainNetwork() {
  // AP 熱點常駐：若因故未啟動則重新開啟
  if (!apMode) startAccessPoint();

  bool sta = (WiFi.status() == WL_CONNECTED);

  if (sta) {
    wifiLostAt = 0;
    return;
  }

  if (wifiSsid.length() == 0) return;  // 尚未設定家用網路，只保留 AP

  if (wifiLostAt == 0) {
    wifiLostAt = millis();
    return;
  }

  if (millis() - wifiLostAt > WIFI_RETRY_MS) {
    wifiLostAt = millis();
    Serial.printf("[WiFi] Reconnecting to %s ...\n", wifiSsid.c_str());
    WiFi.disconnect(false, false);  // 只中斷 STA，AP 保持運作
    beginSta();
  }
}

void updateFaceStatus() {
  static uint32_t last = 0;
  if (millis() - last < 10000) return;
  last = millis();

  if (WiFi.status() == WL_CONNECTED) {
    face.setStatus(WiFi.localIP().toString());
  } else if (apMode) {
    face.setStatus("AP " + WiFi.softAPIP().toString());
  } else {
    face.setStatus("NO WIFI");
  }
}

// ---------------------------------------------------------------------------
// setup / loop
// ---------------------------------------------------------------------------

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println();
  Serial.println("===== DogBot =====");

  pinMode(STATUS_LED, OUTPUT);
  digitalWrite(STATUS_LED, LOW);
  randomSeed(esp_random());

  if (!SPIFFS.begin(true)) {
    Serial.println("[FS] SPIFFS mount failed");
  }
  Serial.printf("[FS] used %u / %u bytes\n",
                (unsigned)SPIFFS.usedBytes(), (unsigned)SPIFFS.totalBytes());

  dog.begin();
  loadServoConfig();

  if (!face.begin()) Serial.println("[OLED] SSD1306 not found");
  face.setStatus("BOOTING...");

  // STA + AP 共存：先開啟 AP 熱點，再非阻塞連線家用 WiFi
  WiFi.mode(WIFI_AP_STA);
  startAccessPoint();

  bool hasConfig = loadWifiConfig(wifiSsid, wifiPass);
  if (hasConfig && wifiSsid.length() > 0) {
    Serial.printf("[WiFi] Connecting to %s ...\n", wifiSsid.c_str());
    face.setStatus("CONNECTING");
    beginSta();
  }

  setupWebServer();

  if (MDNS.begin("dogbot")) {
    MDNS.addService("http", "tcp", 80);
    Serial.println("[mDNS] http://dogbot.local");
  }

  Serial.printf("[HTTP] AP  http://%s/\n", WiFi.softAPIP().toString().c_str());
  Serial.println("[HTTP] STA 連線後可改用裝置 IP 或 http://dogbot.local/");
}

void loop() {
  server.handleClient();
  dog.update();
  face.update();
  maintainNetwork();
  updateFaceStatus();

  // 動作模式改變時同步 OLED 表情
  static DogMode lastMode = MODE_STAND;
  DogMode nowMode = dog.mode();
  if (nowMode != lastMode) {
    lastMode = nowMode;
    switch (nowMode) {
      case MODE_WALK:   face.setMood(MOOD_WALK, 0); break;
      case MODE_SIT:    face.setMood(MOOD_HAPPY, 2500); break;
      case MODE_LIE:    face.setMood(MOOD_SLEEP, 5000); break;
      case MODE_WAVE:   face.setMood(MOOD_EXCITED, 3000); break;
      case MODE_WAG:    face.setMood(MOOD_LOVE, 4000); break;
      case MODE_MANUAL: face.setMood(MOOD_DIZZY, 0); break;
      default:          face.setMood(MOOD_HAPPY, 2000); break;
    }
  }

  if (restartPending && (int32_t)(millis() - restartAt) >= 0) {
    Serial.println("[SYS] Restarting...");
    delay(150);
    ESP.restart();
  }

  delay(1);
}

// ---------------------------------------------------------------------------
// 系統狀態 API
// ---------------------------------------------------------------------------

void appendFileList(String &j) {
  File root = SPIFFS.open("/");
  if (!root) return;

  File f = root.openNextFile();
  bool first = true;
  int count = 0;

  while (f && count < 100) {
    if (!f.isDirectory()) {
      String p = f.name();
      if (!p.startsWith("/")) p = "/" + p;
      if (!first) j += ',';
      first = false;
      j += F("{\"path\":\"");
      j += jsonEscape(p);
      j += F("\",\"size\":");
      j += String((unsigned)f.size());
      j += '}';
      count++;
    }
    f = root.openNextFile();
  }
  root.close();
}

void handleStatus() {
  bool sta = (WiFi.status() == WL_CONNECTED);
  String currentSsid = sta ? WiFi.SSID() : (apMode ? apSsid : wifiSsid);

  String j;
  j.reserve(2000);
  j += F("{\"ok\":true,\"fw\":\"");
  j += FW_VERSION;
  j += F("\",\"name\":\"");
  j += FW_NAME;
  j += F("\",\"wifi\":{\"sta\":");
  j += sta ? F("true") : F("false");
  j += F(",\"ap\":");
  j += apMode ? F("true") : F("false");
  j += F(",\"ssid\":\"");
  j += jsonEscape(currentSsid);
  j += F("\",\"ip\":\"");
  j += sta ? WiFi.localIP().toString() : (apMode ? WiFi.softAPIP().toString() : String("-"));
  j += F("\",\"rssi\":");
  j += String(sta ? WiFi.RSSI() : 0);
  j += F(",\"mac\":\"");
  j += WiFi.macAddress();
  j += F("\",\"apSsid\":\"");
  j += jsonEscape(apSsid);
  j += F("\",\"apIp\":\"");
  j += apMode ? WiFi.softAPIP().toString() : String("-");
  j += F("\",\"apClients\":");
  j += String(apMode ? WiFi.softAPgetStationNum() : 0);
  j += F("},");
  j += F("\"uptime\":");
  j += String(millis() / 1000UL);
  j += F(",\"heap\":");
  j += String(ESP.getFreeHeap());
  j += F(",\"dogMode\":\"");
  j += dog.modeName();
  j += F("\",\"servo\":{\"manual\":");
  j += (dog.mode() == MODE_MANUAL) ? F("true") : F("false");
  j += F(",\"angles\":[");
  for (int i = 0; i < 4; i++) {
    if (i) j += ',';
    j += String(dog.angle(i));
  }
  j += F("],\"trim\":[");
  for (int i = 0; i < 4; i++) {
    if (i) j += ',';
    j += String(dog.trim(i));
  }
  j += F("],\"rev\":[");
  for (int i = 0; i < 4; i++) {
    if (i) j += ',';
    j += dog.reversed(i) ? F("true") : F("false");
  }
  j += F("]},\"fs\":{\"total\":");
  j += String((unsigned)SPIFFS.totalBytes());
  j += F(",\"used\":");
  j += String((unsigned)SPIFFS.usedBytes());
  j += F(",\"free\":");
  j += String((unsigned)(SPIFFS.totalBytes() - SPIFFS.usedBytes()));
  j += F(",\"files\":[");
  appendFileList(j);
  j += F("]}}");

  server.send(200, "application/json; charset=utf-8", j);
}

// ---------------------------------------------------------------------------
// WiFi API
// ---------------------------------------------------------------------------

void handleScan() {
  int n = WiFi.scanNetworks(false, true);

  String j;
  j.reserve(1400);
  j += F("{\"ok\":true,\"networks\":[");
  bool first = true;

  for (int i = 0; i < n; i++) {
    String ssid = WiFi.SSID(i);
    if (ssid.length() == 0) continue;

    // 同 SSID 只保留訊號最強的一筆
    bool duplicate = false;
    for (int k = 0; k < n; k++) {
      if (k != i && WiFi.SSID(k) == ssid && WiFi.RSSI(k) > WiFi.RSSI(i)) {
        duplicate = true;
        break;
      }
    }
    if (duplicate) continue;

    if (!first) j += ',';
    first = false;
    j += F("{\"ssid\":\"");
    j += jsonEscape(ssid);
    j += F("\",\"rssi\":");
    j += String(WiFi.RSSI(i));
    j += F(",\"secure\":");
    j += (WiFi.encryptionType(i) == WIFI_AUTH_OPEN) ? F("false") : F("true");
    j += '}';
  }

  j += F("]}");
  WiFi.scanDelete();
  server.send(200, "application/json; charset=utf-8", j);
}

void handleWifiSave() {
  String ssid = server.hasArg("ssid") ? server.arg("ssid") : "";
  String pass = server.hasArg("pass") ? server.arg("pass") : "";
  ssid.trim();

  if (ssid.length() == 0) {
    server.send(400, "application/json; charset=utf-8",
                "{\"ok\":false,\"error\":\"SSID 不可為空\"}");
    return;
  }

  if (!saveWifiConfig(ssid, pass)) {
    server.send(500, "application/json; charset=utf-8",
                "{\"ok\":false,\"error\":\"無法寫入 SPIFFS\"}");
    return;
  }

  wifiSsid = ssid;
  wifiPass = pass;
  dog.action("stand");

  server.send(200, "application/json; charset=utf-8",
              "{\"ok\":true,\"message\":\"設定已儲存，裝置重新啟動\"}");
  restartPending = true;
  restartAt = millis() + 1000;
}

void handleWifiClear() {
  SPIFFS.remove(WIFI_CONFIG_FILE);
  wifiSsid = "";
  wifiPass = "";

  server.send(200, "application/json; charset=utf-8",
              "{\"ok\":true,\"message\":\"已清除連線設定，裝置重新啟動\"}");
  restartPending = true;
  restartAt = millis() + 1000;
}

// ---------------------------------------------------------------------------
// 動作 / 伺服馬達 API
// ---------------------------------------------------------------------------

void handleMove() {
  float x = server.hasArg("x") ? server.arg("x").toFloat() : 0.0f;
  float y = server.hasArg("y") ? server.arg("y").toFloat() : 0.0f;

  dog.setWalk(y, x);

  String j = F("{\"ok\":true,\"mode\":\"");
  j += dog.modeName();
  j += F("\"}");
  server.send(200, "application/json; charset=utf-8", j);
}

void handleAction() {
  String action = server.hasArg("action") ? server.arg("action") : "";

  if (!dog.action(action)) {
    server.send(400, "application/json; charset=utf-8",
                "{\"ok\":false,\"error\":\"未知的動作指令\"}");
    return;
  }

  String j = F("{\"ok\":true,\"mode\":\"");
  j += dog.modeName();
  j += F("\"}");
  server.send(200, "application/json; charset=utf-8", j);
}

void sendServoState() {
  String j = F("{\"ok\":true,\"manual\":");
  j += (dog.mode() == MODE_MANUAL) ? F("true") : F("false");
  j += F(",\"angles\":[");
  for (int i = 0; i < 4; i++) {
    if (i) j += ',';
    j += String(dog.angle(i));
  }
  j += F("],\"trim\":[");
  for (int i = 0; i < 4; i++) {
    if (i) j += ',';
    j += String(dog.trim(i));
  }
  j += F("],\"rev\":[");
  for (int i = 0; i < 4; i++) {
    if (i) j += ',';
    j += dog.reversed(i) ? F("true") : F("false");
  }
  j += F("],\"mode\":\"");
  j += dog.modeName();
  j += F("\"}");
  server.send(200, "application/json; charset=utf-8", j);
}

void handleServo() {
  if (!server.hasArg("ch") || !server.hasArg("angle")) {
    sendServoState();
    return;
  }

  int ch = server.arg("ch").toInt();
  int angle = server.arg("angle").toInt();

  if (ch < 0 || ch > 3) {
    server.send(400, "application/json; charset=utf-8",
                "{\"ok\":false,\"error\":\"伺服馬達編號必須為 0~3\"}");
    return;
  }

  dog.setManual((uint8_t)ch, angle);
  sendServoState();
}

void handleServoCenter() {
  dog.centerAll();
  sendServoState();
}

void handleServoCalib() {
  int ch = server.hasArg("ch") ? server.arg("ch").toInt() : -1;
  if (ch < 0 || ch > 3) {
    server.send(400, "application/json; charset=utf-8",
                "{\"ok\":false,\"error\":\"伺服馬達編號必須為 0~3\"}");
    return;
  }

  int trim = server.hasArg("trim") ? server.arg("trim").toInt() : dog.trim((uint8_t)ch);
  bool rev = server.hasArg("rev")
                 ? (server.arg("rev") == "1" || server.arg("rev") == "true")
                 : dog.reversed((uint8_t)ch);

  dog.setCalibration((uint8_t)ch, trim, rev);
  saveServoConfig();
  sendServoState();
}

void handleServoCalibReset() {
  for (uint8_t i = 0; i < 4; i++) dog.setCalibration(i, 0, false);
  saveServoConfig();
  sendServoState();
}

void handleReboot() {
  server.send(200, "application/json; charset=utf-8",
              "{\"ok\":true,\"message\":\"裝置重新啟動中\"}");
  restartPending = true;
  restartAt = millis() + 800;
}

// ---------------------------------------------------------------------------
// OTA 更新（韌體 + SPIFFS 映像）
// ---------------------------------------------------------------------------

void otaWriteChunk(HTTPUpload &upload) {
  if (otaMessage.length() > 0) return;

  size_t written = Update.write(upload.buf, upload.currentSize);
  if (written != upload.currentSize) {
    otaMessage = otaFsMode ? "寫入 SPIFFS 失敗" : "寫入韌體失敗";
    Update.printError(Serial);
    Update.abort();
  }
}

void otaStart(HTTPUpload &upload, bool fs) {
  otaSuccess = false;
  otaMessage = "";
  otaFsMode = fs;

  Serial.printf("[OTA] %s start: %s\n", fs ? "SPIFFS" : "firmware",
                upload.filename.c_str());

  if (fs) {
    // 直接寫入 SPIFFS 分割區前先卸載檔案系統
    SPIFFS.end();
  }

  if (!Update.begin(UPDATE_SIZE_UNKNOWN, fs ? U_SPIFFS : U_FLASH)) {
    otaMessage = fs ? "無法開始 SPIFFS 更新" : "無法開始韌體更新";
    Update.printError(Serial);
    if (fs) SPIFFS.begin(true);
  }
}

void otaEnd(HTTPUpload &upload) {
  if (otaMessage.length() == 0) {
    if (Update.end(true)) {
      otaSuccess = true;
      Serial.printf("[OTA] %s success: %u bytes\n",
                    otaFsMode ? "SPIFFS" : "firmware", (unsigned)upload.totalSize);
    } else {
      otaMessage = otaFsMode ? "SPIFFS 映像驗證失敗" : "韌體驗證失敗";
      Update.printError(Serial);
    }
  }

  if (!otaSuccess) {
    if (Update.isRunning()) Update.abort();
    if (otaFsMode && !SPIFFS.begin(true)) Serial.println("[FS] SPIFFS remount failed");
  }
}

void handleOtaUpload() {
  HTTPUpload &upload = server.upload();

  if (upload.status == UPLOAD_FILE_START) {
    otaStart(upload, false);
  } else if (upload.status == UPLOAD_FILE_END) {
    otaEnd(upload);
  } else if (upload.status == UPLOAD_FILE_ABORTED) {
    Update.abort();
    otaMessage = "上傳已中止";
    if (otaFsMode) SPIFFS.begin(true);
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    otaWriteChunk(upload);
  }
}

void handleFsOtaUpload() {
  HTTPUpload &upload = server.upload();

  if (upload.status == UPLOAD_FILE_START) {
    otaStart(upload, true);
  } else if (upload.status == UPLOAD_FILE_END) {
    otaEnd(upload);
  } else if (upload.status == UPLOAD_FILE_ABORTED) {
    Update.abort();
    otaMessage = "上傳已中止";
    SPIFFS.begin(true);
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    otaWriteChunk(upload);
  }
}

void handleOtaFinish() {
  const char *label = otaFsMode ? "SPIFFS" : "韌體";

  if (otaSuccess) {
    String j = F("{\"ok\":true,\"message\":\"");
    j += label;
    j += F(" 更新成功，裝置重新啟動\"}");
    server.send(200, "application/json; charset=utf-8", j);
    restartPending = true;
    restartAt = millis() + 1500;
  } else {
    String msg = otaMessage.length() ? otaMessage : "更新失敗";
    String j = F("{\"ok\":false,\"error\":\"");
    j += jsonEscape(msg);
    j += F("\"}");
    server.send(500, "application/json; charset=utf-8", j);
  }
}

// ---------------------------------------------------------------------------
// 靜態網頁（SPIFFS）
// ---------------------------------------------------------------------------

const char FALLBACK_HTML[] PROGMEM = R"HTML(<!DOCTYPE html>
<html lang="zh-Hant"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>DogBot - SPIFFS 尚未上傳</title>
<style>
body{font-family:system-ui,"Microsoft JhengHei",sans-serif;background:#0b0f17;color:#e8eefc;
display:flex;align-items:center;justify-content:center;min-height:100vh;margin:0;padding:24px}
.box{max-width:560px;background:#151c2c;border:1px solid #2a3550;border-radius:18px;padding:32px}
h1{margin:0 0 8px;font-size:24px}code{background:#0b0f17;padding:2px 6px;border-radius:6px}
ol{line-height:1.9;color:#b9c4d8}li strong{color:#ffc857}
</style></head><body><div class="box">
<h1>🐶 找不到前端檔案</h1>
<p>SPIFFS 內沒有 <code>/index.html</code>，請先上傳 <code>data/</code> 資料夾。</p>
<ol>
<li>安裝 <strong>ESP32 Sketch Data Upload</strong> 外掛</li>
<li>Arduino IDE 選擇正確的開發板與序列埠</li>
<li>Tools → <strong>ESP32 Sketch Data Upload</strong></li>
<li>完成後重新啟動裝置</li>
</ol>
</div></body></html>)HTML";

void handleStatic() {
  String path = server.uri();

  if (path.startsWith("/api/")) {
    server.send(404, "application/json; charset=utf-8",
                "{\"ok\":false,\"error\":\"API not found\"}");
    return;
  }
  if (path.indexOf("..") >= 0) {
    server.send(400, "text/plain; charset=utf-8", "Bad request");
    return;
  }
  if (path == "/") path = "/index.html";

  if (SPIFFS.exists(path)) {
    File f = SPIFFS.open(path, "r");
    if (f) {
      server.sendHeader("Cache-Control", "no-store");
      server.streamFile(f, mimeType(path));
      f.close();
      return;
    }
  }

  if (path == "/index.html") {
    server.send_P(200, "text/html; charset=utf-8", FALLBACK_HTML);
    return;
  }

  server.send(404, "text/plain; charset=utf-8", "404 Not Found");
}

// ---------------------------------------------------------------------------
// 路由註冊
// ---------------------------------------------------------------------------

void setupWebServer() {
  server.on("/api/status", HTTP_GET, handleStatus);
  server.on("/api/wifi/scan", HTTP_GET, handleScan);
  server.on("/api/wifi", HTTP_POST, handleWifiSave);
  server.on("/api/wifi/clear", HTTP_POST, handleWifiClear);
  server.on("/api/move", HTTP_POST, handleMove);
  server.on("/api/action", HTTP_POST, handleAction);
  server.on("/api/servo", HTTP_POST, handleServo);
  server.on("/api/servo/center", HTTP_POST, handleServoCenter);
  server.on("/api/servo/calib", HTTP_POST, handleServoCalib);
  server.on("/api/servo/calib/reset", HTTP_POST, handleServoCalibReset);
  server.on("/api/reboot", HTTP_POST, handleReboot);
  server.on("/api/ota", HTTP_POST, handleOtaFinish, handleOtaUpload);
  server.on("/api/ota/fs", HTTP_POST, handleOtaFinish, handleFsOtaUpload);

  server.onNotFound(handleStatic);

  server.begin();
  Serial.println("[HTTP] Server started");
}
