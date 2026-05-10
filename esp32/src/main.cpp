#include <SPI.h>
#include <Wire.h>
#include <MFRC522.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <HX711_ADC.h>

#define RST_PIN     16
#define SS_PIN       5
#define HX711_DT     4
#define HX711_SCK    2
#define BUTTON_PIN   0

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

MFRC522 mfrc522(SS_PIN, RST_PIN);
HX711_ADC LoadCell(HX711_DT, HX711_SCK);

const char* ssid      = "esp32";
const char* password  = "qwertyui";
const char* serverURL = "https://warehouse-1w.onrender.com";

bool isAuthorized = false;
String workerRFID = "";

String lastRegisteredRFID = "";
unsigned long lastRegistrationTime = 0;
const unsigned long REGISTRATION_COOLDOWN = 8000;

float calibrationValue = 203.0;
float currentWeight    = 0.0;

String currentBarcode = "";
String currentProduct = "";
float  unitWeight     = 0.0;
float  tareWeight     = 0.0;

bool weighModeActive = false;

void logLine(const String& message) {
  Serial.println(message);
}

// Яскравість OLED: 4 рівні, перемикаються кнопкою поза зважуванням
const uint8_t brightLevels[] = { 5, 50, 150, 255 };
const int numBrightLevels = 4;
int brightIndex = 2;  // початковий рівень (150)

void setBrightness(int idx) {
  display.ssd1306_command(SSD1306_SETCONTRAST);
  display.ssd1306_command(brightLevels[idx]);
}

// Апаратний переривач для кнопки — ловить натискання навіть під час HTTP-запитів
volatile bool buttonPressed = false;
volatile unsigned long lastButtonISRTime = 0;

void IRAM_ATTR onButtonPress() {
  unsigned long now = millis();
  if (now - lastButtonISRTime > 300) {
    buttonPressed = true;
    lastButtonISRTime = now;
  }
}

enum State { WAIT_RFID, READY, WEIGHING };
State systemState = WAIT_RFID;

unsigned long lastOledCheck    = 0;
unsigned long lastWeighSend    = 0;
unsigned long lastWeighOled    = 0;
unsigned long lastPendingCheck = 0;
unsigned long lastSessionCheck = 0;
unsigned long lastStableCheck  = 0;
unsigned long lastWaitSessionCheck = 0;
unsigned long lastAutoConfTime = 0;

float lastStableWeight = 0.0;
int   stableCount      = 0;

// Transliterate UTF-8 Ukrainian → ASCII for OLED display
String translit(String s) {
  const uint8_t* b = (const uint8_t*)s.c_str();
  int len = s.length();
  String r = "";
  for (int i = 0; i < len; ) {
    uint8_t c = b[i];
    if (c == 0xD0 && i + 1 < len) {
      switch (b[i+1]) {
        case 0x84: r += "Ye"; break;
        case 0x86: r += "I";  break;
        case 0x87: r += "Yi"; break;
        case 0x90: r += "A";  break; case 0xB0: r += "a";  break;
        case 0x91: r += "B";  break; case 0xB1: r += "b";  break;
        case 0x92: r += "V";  break; case 0xB2: r += "v";  break;
        case 0x93: r += "H";  break; case 0xB3: r += "h";  break;
        case 0x94: r += "D";  break; case 0xB4: r += "d";  break;
        case 0x95: r += "E";  break; case 0xB5: r += "e";  break;
        case 0x96: r += "Zh"; break; case 0xB6: r += "zh"; break;
        case 0x97: r += "Z";  break; case 0xB7: r += "z";  break;
        case 0x98: r += "Y";  break; case 0xB8: r += "y";  break;
        case 0x99: r += "Y";  break; case 0xB9: r += "y";  break;
        case 0x9A: r += "K";  break; case 0xBA: r += "k";  break;
        case 0x9B: r += "L";  break; case 0xBB: r += "l";  break;
        case 0x9C: r += "M";  break; case 0xBC: r += "m";  break;
        case 0x9D: r += "N";  break; case 0xBD: r += "n";  break;
        case 0x9E: r += "O";  break; case 0xBE: r += "o";  break;
        case 0x9F: r += "P";  break; case 0xBF: r += "p";  break;
        case 0xA0: r += "R";  break;
        case 0xA1: r += "S";  break;
        case 0xA2: r += "T";  break;
        case 0xA3: r += "U";  break;
        case 0xA4: r += "F";  break;
        case 0xA5: r += "Kh"; break;
        case 0xA6: r += "Ts"; break;
        case 0xA7: r += "Ch"; break;
        case 0xA8: r += "Sh"; break;
        case 0xA9: r += "Shch"; break;
        case 0xAC: break;
        case 0xAE: r += "Yu"; break;
        case 0xAF: r += "Ya"; break;
        default: break;
      }
      i += 2;
    } else if (c == 0xD1 && i + 1 < len) {
      switch (b[i+1]) {
        case 0x80: r += "r";    break;
        case 0x81: r += "s";    break;
        case 0x82: r += "t";    break;
        case 0x83: r += "u";    break;
        case 0x84: r += "f";    break;
        case 0x85: r += "kh";   break;
        case 0x86: r += "ts";   break;
        case 0x87: r += "ch";   break;
        case 0x88: r += "sh";   break;
        case 0x89: r += "shch"; break;
        case 0x8C: break;
        case 0x8E: r += "yu";   break;
        case 0x8F: r += "ya";   break;
        case 0x94: r += "ye";   break;
        case 0x96: r += "i";    break;
        case 0x97: r += "yi";   break;
        default: break;
      }
      i += 2;
    } else if (c == 0xD2 && i + 1 < len) {
      if (b[i+1] == 0x90) r += "G";
      else if (b[i+1] == 0x91) r += "g";
      i += 2;
    } else if (c < 0x80) {
      r += (char)c;
      i++;
    } else {
      i++;
      while (i < len && (b[i] & 0xC0) == 0x80) i++;
    }
  }
  return r;
}

// ════════════════════════════════════════════════════════
void showOLED(String line1, String line2 = "", String line3 = "") {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);

  int count = (line1.length() > 0 ? 1 : 0) +
              (line2.length() > 0 ? 1 : 0) +
              (line3.length() > 0 ? 1 : 0);

  // Vertical: center block of lines (each line 10px tall)
  int blockH = count * 10;
  int startY = (64 - blockH) / 2;

  int y = startY;
  if (line1.length() > 0) {
    int x = (128 - (int)line1.length() * 6) / 2;
    if (x < 0) x = 0;
    display.setCursor(x, y);
    display.print(line1);
    y += 10;
  }
  if (line2.length() > 0) {
    int x = (128 - (int)line2.length() * 6) / 2;
    if (x < 0) x = 0;
    display.setCursor(x, y);
    display.print(line2);
    y += 10;
  }
  if (line3.length() > 0) {
    int x = (128 - (int)line3.length() * 6) / 2;
    if (x < 0) x = 0;
    display.setCursor(x, y);
    display.print(line3);
  }
  display.display();
}

void showWeighOLED() {
  float net = currentWeight - tareWeight;
  if (net < 0) net = 0;
  int qty = (unitWeight > 0) ? (int)(net / unitWeight) : 0;
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1); display.setCursor(0, 0);
  display.println(currentProduct.substring(0, 16));
  display.setTextSize(2); display.setCursor(0, 16);
  display.print(net, 1); display.setTextSize(1); display.print(" g");
  display.setCursor(0, 48);
  display.print("Qty: "); display.print(qty); display.print(" pcs");
  display.display();
}

// ════════════════════════════════════════════════════════
bool fetchProduct(String barcode) {
  HTTPClient http;
  String url = String(serverURL) + "/products/barcode/" + barcode;
  logLine("HTTP GET " + url);
  http.begin(url);
  http.setTimeout(2000);
  int code = http.GET();
  logLine("HTTP GET code: " + String(code));
  if (code == 200) {
    String body = http.getString();
    int ns = body.indexOf("\"name\":\"") + 8;
    int ne = body.indexOf("\"", ns);
    currentProduct = body.substring(ns, ne);
    int us = body.indexOf("\"unit_weight\":") + 14;
    int ue = body.indexOf(",", us);
    if (ue == -1) ue = body.indexOf("}", us);
    unitWeight = body.substring(us, ue).toFloat();
    http.end();
    return true;
  }
  http.end();
  return false;
}

bool saveOperation(int quantity, float grossWeight) {
  HTTPClient http;
  String url = String(serverURL) + "/operations/";
  logLine("HTTP POST " + url);
  http.begin(url);
  http.addHeader("Content-Type", "application/json");
  String body = "{\"barcode\":\"" + currentBarcode + "\",";
  body += "\"quantity\":" + String(quantity) + ",";
  body += "\"gross_weight\":" + String(grossWeight, 2) + ",";
  body += "\"tare_weight\":" + String(tareWeight, 2) + ",";
  body += "\"worker_rfid\":\"" + workerRFID + "\",";
  body += "\"type\":\"incoming\"}";
  int code = http.POST(body);
  logLine("HTTP POST code: " + String(code));
  http.end();
  return (code == 200 || code == 201);
}

void connectWiFi() {
  logLine("Booting firmware");
  logLine("Target server: " + String(serverURL));
  logLine("Connecting to WiFi SSID: " + String(ssid));
  showOLED("Connecting WiFi...");
  WiFi.begin(ssid, password);
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    logLine("WiFi status=" + String(WiFi.status()) + " attempt=" + String(attempts + 1));
    delay(500); attempts++;
  }
  if (WiFi.status() == WL_CONNECTED) {
    logLine("WiFi connected");
    logLine("IP: " + WiFi.localIP().toString());
    showOLED("WiFi OK", WiFi.localIP().toString());
    delay(1500);
    showOLED("Welcome to", "Warehouse", "System");
    delay(1500);
  } else {
    logLine("WiFi failed");
    showOLED("WiFi FAILED", "Check settings");
    delay(2000);
  }
}

void resetToLogin() {
  isAuthorized = false;
  workerRFID = "";
  currentBarcode = ""; currentProduct = "";
  unitWeight = 0.0; tareWeight = 0.0;
  systemState = WAIT_RFID;
  showOLED("Scan RFID", "to login");
}

// ════════════════════════════════════════════════════════
void setup() {
  Serial.begin(115200);
  delay(300);
  logLine("Serial ready at 115200");
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(BUTTON_PIN), onButtonPress, FALLING);
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) { for (;;); }
  showOLED("Booting...");
  delay(500);
  SPI.begin();
  mfrc522.PCD_Init();
  LoadCell.begin();
  LoadCell.start(2000, true);
  LoadCell.setCalFactor(calibrationValue);
  LoadCell.setSamplesInUse(1);   // вимикаємо EMA-згладжування — миттєвий відгук
  connectWiFi();
  LoadCell.tareNoDelay();
  while (!LoadCell.getTareStatus()) { LoadCell.update(); }
  systemState = WAIT_RFID;
  showOLED("Scan RFID", "to login");
}

// ════════════════════════════════════════════════════════
void loop() {

  // Зчитування ваги — агресивно, щоб мати свіжий зразок
  for (int i = 0; i < 20; i++) {
    if (LoadCell.update()) { currentWeight = LoadCell.getData(); break; }
    delay(8);
  }

  // ── WAIT_RFID: тільки читаємо картку, жодних HTTP ────
  if (systemState == WAIT_RFID) {

    // Перевіряємо сесію кожні 5с — якщо вже є активна, переходимо в READY
    if (millis() - lastWaitSessionCheck > 5000) {
      lastWaitSessionCheck = millis();
      HTTPClient httpSess;
      String sessionUrl = String(serverURL) + "/session/";
      logLine("Polling session: " + sessionUrl);
      httpSess.begin(sessionUrl);
      httpSess.setTimeout(2000);
      int sc = httpSess.GET();
      logLine("Session poll code: " + String(sc));
      if (sc == 200) {
        String sb = httpSess.getString();
        if (sb.indexOf("\"rfid\":null") < 0 && sb.indexOf("\"rfid\":\"\"") < 0) {
          String workerName = "";
          int ni = sb.indexOf("\"name\":\"");
          if (ni >= 0) { int s = ni+8, e = sb.indexOf("\"",s); if(e>s) workerName = sb.substring(s,e); }
          String rfidStr = "";
          int ri = sb.indexOf("\"rfid\":\"");
          if (ri >= 0) { int s = ri+8, e = sb.indexOf("\"",s); if(e>s) rfidStr = sb.substring(s,e); }
          workerRFID = rfidStr;
          isAuthorized = true;
          systemState = READY;
          httpSess.end();
          showOLED("Welcome back!", translit(workerName), "Use website");
          return;
        }
      }
      httpSess.end();
    }

    if (!mfrc522.PICC_IsNewCardPresent()) return;
    if (!mfrc522.PICC_ReadCardSerial()) return;

    String scannedRFID = "";
    for (byte i = 0; i < mfrc522.uid.size; i++) {
      if (mfrc522.uid.uidByte[i] < 0x10) scannedRFID += "0";
      scannedRFID += String(mfrc522.uid.uidByte[i], HEX);
    }
    scannedRFID.toUpperCase();

    // Перевіряємо режим реєстрації
    HTTPClient httpRegMode;
    httpRegMode.begin(String(serverURL) + "/rfid/register-mode/");
    httpRegMode.setTimeout(800);
    int regCode = httpRegMode.GET();
    logLine("Register mode code: " + String(regCode));
    String regResp = httpRegMode.getString();
    httpRegMode.end();

    bool isRegisterMode = (regCode == 200 && regResp.indexOf("\"active\":true") >= 0);

    if (isRegisterMode) {
      HTTPClient httpReg;
      httpReg.begin(String(serverURL) + "/rfid/scanned/");
      httpReg.addHeader("Content-Type", "application/json");
      httpReg.setTimeout(2000);
      int postCode = httpReg.POST("{\"rfid\":\"" + scannedRFID + "\"}");
      logLine("RFID register code: " + String(postCode));
      httpReg.end();
      lastRegisteredRFID = scannedRFID;
      lastRegistrationTime = millis();
      showOLED("Card saved!", scannedRFID, "Enter name on site");
      mfrc522.PICC_HaltA();
      mfrc522.PCD_StopCrypto1();
      delay(3000);
      showOLED("Scan RFID", "to login");
      return;
    }

    // Перевіряємо режим логіну — якщо не натиснуто "Увійти", ігноруємо
    HTTPClient httpLoginMode;
    httpLoginMode.begin(String(serverURL) + "/rfid/login-mode/");
    httpLoginMode.setTimeout(800);
    int loginCode = httpLoginMode.GET();
    logLine("Login mode code: " + String(loginCode));
    String loginResp = httpLoginMode.getString();
    httpLoginMode.end();

    bool isLoginMode = (loginCode == 200 && loginResp.indexOf("\"active\":true") >= 0);

    if (!isLoginMode) {
      // Кнопка "Увійти" не натиснута — ігноруємо картку
      mfrc522.PICC_HaltA();
      mfrc522.PCD_StopCrypto1();
      return;
    }

    // Пропускаємо login якщо це картка щойно зареєстрованого працівника
    if (scannedRFID == lastRegisteredRFID &&
        (millis() - lastRegistrationTime) < REGISTRATION_COOLDOWN) {
      mfrc522.PICC_HaltA();
      mfrc522.PCD_StopCrypto1();
      return;
    }

    // Режим логіну активний — перевіряємо працівника в БД
    HTTPClient httpWorker;
    httpWorker.begin(String(serverURL) + "/workers/" + scannedRFID + "/");
    httpWorker.setTimeout(2000);
    int workerCode = httpWorker.GET();
    logLine("Worker lookup code: " + String(workerCode));
    String workerBody = (workerCode == 200) ? httpWorker.getString() : "";
    httpWorker.end();

    if (workerCode == 200) {
      // Parse name from JSON response
      String workerName = "";
      int nameIdx = workerBody.indexOf("\"name\":\"");
      if (nameIdx >= 0) {
        int start = nameIdx + 8;
        int end = workerBody.indexOf("\"", start);
        if (end > start) workerName = workerBody.substring(start, end);
      }
      workerRFID = scannedRFID;
      isAuthorized = true;
      systemState = READY;
      showOLED("Access Granted", translit(workerName), "Use website");
    } else {
      showOLED("Access DENIED", "Bad tag");
      delay(1500);
      showOLED("Scan RFID", "to login");
    }

    mfrc522.PICC_HaltA();
    mfrc522.PCD_StopCrypto1();
    return;
  }

  // ── READY: авторизований, чекаємо команди ────────────
  if (systemState == READY) {

    // Перевірка сесії — чи не вийшов з сайту
    if (millis() - lastSessionCheck > 3000) {
      lastSessionCheck = millis();
      HTTPClient http;
      http.begin(String(serverURL) + "/session/");
      http.setTimeout(1000);
      int code = http.GET();
      String body = http.getString();
      http.end();
      if (code != 200 || body.indexOf("\"rfid\":null") >= 0) {
        resetToLogin(); return;
      }
    }

    // OLED повідомлення від сайту
    if (millis() - lastOledCheck > 2000) {
      lastOledCheck = millis();
      HTTPClient http;
      http.begin(String(serverURL) + "/oled/");
      http.setTimeout(800);
      int code = http.GET();
      if (code == 200) {
        String body = http.getString();
        if (body.indexOf("\"updated\":true") >= 0) {
          int s1 = body.indexOf("\"line1\":\"") + 9; int e1 = body.indexOf("\"", s1);
          int s2 = body.indexOf("\"line2\":\"") + 9; int e2 = body.indexOf("\"", s2);
          int s3 = body.indexOf("\"line3\":\"") + 9; int e3 = body.indexOf("\"", s3);
          showOLED(body.substring(s1,e1), body.substring(s2,e2), body.substring(s3,e3));
        }
      }
      http.end();
    }

     // Відправка ваги на сервер
     if (millis() - lastWeighSend > 400) {
       lastWeighSend = millis();
       HTTPClient http;
       http.begin(String(serverURL) + "/weight/current/");
       http.addHeader("Content-Type", "application/json");
       http.setTimeout(2000);
      int wCode = http.POST("{\"weight\":" + String(currentWeight, 2) + "}");
       if (wCode == 200) {
         String wResp = http.getString();
        weighModeActive = (wResp.indexOf("\"active\":true") >= 0);
       }
       http.end();
     }

     // OLED оновлення ваги в реальному часі (кожні 200мс)
     if (weighModeActive && millis() - lastWeighOled > 200) {
       lastWeighOled = millis();
       // Свіже зчитування перед показом
       for (int i = 0; i < 15; i++) {
         if (LoadCell.update()) { currentWeight = LoadCell.getData(); break; }
         delay(10);
       }
       float displayWeight = max(0.0f, currentWeight);
       showOLED("Weight:", String(displayWeight, 1) + "g", "Wait stable...");
     }

     // Авто-підтвердження ваги при стабілізації (кожні 120мс)
     if (weighModeActive && millis() - lastStableCheck > 120) {
       lastStableCheck = millis();
       if (currentWeight > 1.0 && abs(currentWeight - lastStableWeight) < 2.0) {
         stableCount++;
       } else {
         stableCount = 0;
         lastStableWeight = currentWeight;
       }
       if (stableCount >= 2 && millis() - lastAutoConfTime > 3000) {
         lastAutoConfTime = millis();
         stableCount = 0;
         HTTPClient httpConf;
         httpConf.begin(String(serverURL) + "/weight/confirmed/");
         httpConf.addHeader("Content-Type", "application/json");
         httpConf.setTimeout(1000);
         httpConf.POST("{\"weight\":" + String(currentWeight, 2) + "}");
         httpConf.end();
         showOLED("Weight:", String(max(0.0f, currentWeight), 1) + "g", "Confirmed!");
       }
     }

     // Перевірка pending зважування
     if (millis() - lastPendingCheck > 700) {
       lastPendingCheck = millis();
       HTTPClient http;
       http.begin(String(serverURL) + "/weigh/pending/");
       http.setTimeout(800);
       int code = http.GET();
       if (code == 200) {
         String body = http.getString();
          if (body.indexOf("null") < 0 && body.indexOf("\"barcode\":\"") >= 0) {
            int bs = body.indexOf("\"barcode\":\"") + 11;
            int be = body.indexOf("\"", bs);
           String barcode = body.substring(bs, be);
           if (barcode.length() > 0) {
             if (fetchProduct(barcode)) {
               currentBarcode = barcode;
               systemState = WEIGHING;
               tareWeight = 0;
               stableCount = 0;
               lastStableWeight = 0.0;
               showOLED(currentProduct.substring(0,16), String(currentWeight, 1) + "g", "Wait stable...");
               // Підтверджуємо
               HTTPClient httpC;
               httpC.begin(String(serverURL) + "/weigh/confirm/");
               httpC.addHeader("Content-Type", "application/json");
               httpC.POST("{}");
               httpC.end();
             }
           }
         }
       }
       http.end();
     }


     // Кнопка — лише яскравість OLED
     if (buttonPressed) {
       buttonPressed = false;
       brightIndex = (brightIndex + 1) % numBrightLevels;
       setBrightness(brightIndex);
     }

    // Перевірка RFID для режиму реєстрації нового працівника
    if (mfrc522.PICC_IsNewCardPresent() && mfrc522.PICC_ReadCardSerial()) {
      String scannedRFID = "";
      for (byte i = 0; i < mfrc522.uid.size; i++) {
        if (mfrc522.uid.uidByte[i] < 0x10) scannedRFID += "0";
        scannedRFID += String(mfrc522.uid.uidByte[i], HEX);
      }
      scannedRFID.toUpperCase();
      HTTPClient httpMode;
      httpMode.begin(String(serverURL) + "/rfid/register-mode/");
      httpMode.setTimeout(1000);
      int modeCode = httpMode.GET();
      String modeResp = httpMode.getString();
      httpMode.end();
      if (modeCode == 200 && modeResp.indexOf("\"active\":true") >= 0) {
        HTTPClient httpReg;
        httpReg.begin(String(serverURL) + "/rfid/scanned/");
        httpReg.addHeader("Content-Type", "application/json");
        httpReg.setTimeout(2000);
        httpReg.POST("{\"rfid\":\"" + scannedRFID + "\"}");
        httpReg.end();
        showOLED("Card saved!", scannedRFID, "Enter name on site");
      }
      mfrc522.PICC_HaltA();
      mfrc522.PCD_StopCrypto1();
    }
    return;
  }

  // ── WEIGHING: зважування ─────────────────────────────
  if (systemState == WEIGHING) {

    // Відправка ваги + перевірка чи режим ще активний
    if (millis() - lastWeighSend > 400) {
      lastWeighSend = millis();
      HTTPClient http;
      http.begin(String(serverURL) + "/weight/current/");
      http.addHeader("Content-Type", "application/json");
      http.setTimeout(2000);
      int wCode = http.POST("{\"weight\":" + String(currentWeight, 2) + "}");
      if (wCode == 200) {
        String wResp = http.getString();
        if (wResp.indexOf("\"active\":false") >= 0) {
          http.end();
          currentBarcode = ""; currentProduct = "";
          unitWeight = 0.0; tareWeight = 0.0;
          systemState = READY;
          showOLED("Ready", "Use website", "");
          return;
        }
      }
      http.end();
    }

    // Показуємо вагу в реальному часі
    if (millis() - lastWeighOled > 200) {
      lastWeighOled = millis();
      // Свіже зчитування перед показом
      for (int i = 0; i < 15; i++) {
        if (LoadCell.update()) { currentWeight = LoadCell.getData(); break; }
        delay(10);
      }
      showOLED(currentProduct.substring(0,16), String(currentWeight, 1) + "g", "Wait stable...");
    }

    // Кнопка — лише яскравість OLED
    if (buttonPressed) {
      buttonPressed = false;
      brightIndex = (brightIndex + 1) % numBrightLevels;
      setBrightness(brightIndex);
    }

    // Авто-збереження при стабілізації (кожні 120мс)
    if (millis() - lastStableCheck > 120) {
      lastStableCheck = millis();
      if (currentWeight > 1.0 && abs(currentWeight - lastStableWeight) < 3.0) {
        stableCount++;
      } else {
        stableCount = 0;
        lastStableWeight = currentWeight;
      }
      if (stableCount >= 2) {
        stableCount = 0;
        float net = currentWeight;
        if (net < 0) net = 0;
        int qty = (unitWeight > 0) ? (int)(net / unitWeight) : 0;
        showOLED("Saving...", String(qty) + " pcs", String(net, 1) + "g");
        HTTPClient httpConf;
        httpConf.begin(String(serverURL) + "/weight/confirmed/");
        httpConf.addHeader("Content-Type", "application/json");
        httpConf.setTimeout(1000);
        httpConf.POST("{\"weight\":" + String(net, 2) + "}");
        httpConf.end();
        if (saveOperation(qty, net)) {
          showOLED("SAVED!", String(qty) + " pcs", currentProduct.substring(0,16));
        } else {
          showOLED("SAVE ERROR", "Check server");
        }
        delay(1500);
        currentBarcode = ""; currentProduct = "";
        unitWeight = 0.0; tareWeight = 0.0;
        systemState = READY;
        showOLED("Ready", "Use website", "");
      }
    }
    return;
  }
}
