// =====================================================================
// ESP32 + TCS3200 Color Standard Inspector
// - ปุ่ม Set Standard / Check Color ทำงานผ่าน Interrupt (ไม่ blocking)
// - Calibration ด้วยแผ่นขาว/ดำ ผ่านคำสั่ง Serial ('W' = white, 'K' = black)
// - แปลงค่า RGB (0-255) -> CIE XYZ -> CIE L*a*b*
// - คำนวณ Delta E (CIE76) เทียบกับค่ามาตรฐานที่บันทึกไว้
// - DHT11 เช็คสภาพแวดล้อม (แจ้งเตือนแต่ไม่หยุดทำงาน)
// - ทุกอย่างเขียนแบบ non-blocking ด้วย millis() ยกเว้น pulseIn()
//   ซึ่งมี timeout จำกัดอยู่แล้ว (ไม่เกิน 50ms ต่อครั้ง)
// =====================================================================

#include <Arduino.h>
#include "DHT.h"
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <math.h>

// ---------------- DHT11 ----------------
#define DHTTYPE DHT11
const int DHTPIN = 4;
DHT dht(DHTPIN, DHTTYPE);

// ช่วงสภาพแวดล้อมที่เหมาะสมสำหรับการวัดสี
constexpr float TEMP_MIN = 20.0f;
constexpr float TEMP_MAX = 25.0f;
constexpr float HUM_MIN  = 50.0f;
constexpr float HUM_MAX  = 60.0f;

// ---------------- OLED SSD1306 ----------------
const int SCREEN_WIDTH = 128;
const int SCREEN_HEIGHT = 64;
const int OLED_RESET = -1;
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
bool oledOK = false;

// ---------------- LED สถานะ ----------------
const int LED_RED = 27;
const int LED_GREEN = 13;
const int LED_YELLOW = 14;

// ---------------- RGB LED (แสดงสีที่วัดได้) ----------------
const int LED_R = 5;
const int LED_G = 23;
const int LED_B = 15;

// ---------------- ปุ่มกด (Interrupt) ----------------
const int button_set_standard = 18;
const int button_check_color = 19;
constexpr uint32_t BUTTON_DEBOUNCE_US = 250000UL; // 250ms กันเด้ง

volatile bool requestSetStandard = false;
volatile bool requestCheckColor = false;
volatile uint32_t lastSetInterruptUs = 0;
volatile uint32_t lastCheckInterruptUs = 0;

void IRAM_ATTR onSetStandardButton() {
  uint32_t now = micros();
  if (now - lastSetInterruptUs > BUTTON_DEBOUNCE_US) {
    requestSetStandard = true;
    lastSetInterruptUs = now;
  }
}

void IRAM_ATTR onCheckColorButton() {
  uint32_t now = micros();
  if (now - lastCheckInterruptUs > BUTTON_DEBOUNCE_US) {
    requestCheckColor = true;
    lastCheckInterruptUs = now;
  }
}

// ---------------- TCS3200 ----------------
const int S0 = 32;
const int S1 = 33;
const int S2 = 25;
const int S3 = 26;
const int Out = 34;
constexpr unsigned long PULSE_TIMEOUT_US = 50000UL;

// ค่า Calibration (ความถี่ดิบ Min/Max ต่อสี จากแผ่นขาว/ดำ)
// ต้อง calibrate ก่อนใช้งานจริง ไม่งั้นค่า RGB ที่แปลงได้จะไม่แม่นยำ
struct CalibrationData {
  unsigned long redMin, redMax;
  unsigned long greenMin, greenMax;
  unsigned long blueMin, blueMax;
  bool calibrated = false;
};
CalibrationData calib;

// ---------------- ค่าสี ----------------
struct RGBColor { uint8_t r, g, b; };
struct LabColor { float L, a, b; };

bool hasStandard = false;
LabColor standardLab;
RGBColor standardRgb;

// เกณฑ์ยอมรับ Delta E: ตามมาตรฐาน CIE ทั่วไป
//   dE < 1   แทบแยกด้วยตาไม่ได้
//   dE 1-2   แยกได้เฉพาะสายตาฝึกมา
//   dE 2-10  แยกได้ด้วยตาเปล่าทั่วไป
//   dE > 10  สีต่างกันชัดเจน
// ใช้ 5.0 เป็นเกณฑ์ผ่าน/ไม่ผ่านสำหรับงาน QC ทั่วไป ปรับได้ตามความละเอียดที่ต้องการ
constexpr float DELTA_E_THRESHOLD = 5.0f;

// ---------------- State Machine (non-blocking) ----------------
enum AppState { STATE_IDLE, STATE_MEASURE_STANDARD, STATE_MEASURE_CHECK };
AppState appState = STATE_IDLE;

constexpr unsigned long MEASURE_DURATION_MS = 5000; // วัด 5 วินาที
unsigned long measureStartMs = 0;
unsigned long sumRed = 0, sumGreen = 0, sumBlue = 0;
uint16_t sampleCount = 0;

// ไฟเหลืองกระพริบระหว่างวัด (non-blocking)
unsigned long lastBlinkMs = 0;
bool yellowState = false;
constexpr unsigned long BLINK_INTERVAL_MS = 250;

// พิมพ์ผลออก Serial ทุก 1 วินาทีระหว่างวัด
unsigned long lastPrintMs = 0;
constexpr unsigned long PRINT_INTERVAL_MS = 1000;

// เช็คสภาพแวดล้อมทุก 2 วินาที (DHT11 อ่านถี่กว่านี้ไม่ได้)
unsigned long lastEnvCheckMs = 0;
constexpr unsigned long ENV_CHECK_INTERVAL_MS = 2000;

// ---------------- Prototypes ----------------
unsigned long readColorFrequency(bool s2State, bool s3State);
RGBColor frequencyToRGB(unsigned long r, unsigned long g, unsigned long b);
LabColor rgbToLab(const RGBColor &rgb);
float calculateDeltaE(const LabColor &c1, const LabColor &c2);
void setStatusLed(bool green, bool yellow, bool red);
void showRgbOnLed(const RGBColor &rgb);
void checkEnvironment();
void handleSerialCalibration();
void startMeasurement(AppState target);
void updateMeasurement();
void finishMeasurement();
void oledMessage(const String &line1, const String &line2 = "", const String &line3 = "");

void setup() {
  Serial.begin(115200);
  delay(300);

  dht.begin();
  Wire.begin(21, 22);
  Wire.setTimeOut(1000);
  oledOK = display.begin(SSD1306_SWITCHCAPVCC, 0x3C);

  pinMode(LED_RED, OUTPUT);
  pinMode(LED_GREEN, OUTPUT);
  pinMode(LED_YELLOW, OUTPUT);
  pinMode(LED_R, OUTPUT);
  pinMode(LED_G, OUTPUT);
  pinMode(LED_B, OUTPUT);
  setStatusLed(false, false, false);
  showRgbOnLed({0, 0, 0});

  pinMode(S0, OUTPUT);
  pinMode(S1, OUTPUT);
  pinMode(S2, OUTPUT);
  pinMode(S3, OUTPUT);
  pinMode(Out, INPUT);
  digitalWrite(S0, HIGH); // Output Frequency Scaling 20% (เสถียรที่สุด)
  digitalWrite(S1, LOW);

  pinMode(button_set_standard, INPUT_PULLUP);
  pinMode(button_check_color, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(button_set_standard), onSetStandardButton, FALLING);
  attachInterrupt(digitalPinToInterrupt(button_check_color), onCheckColorButton, FALLING);

  Serial.println(F("=== Color Standard Inspector ==="));
  Serial.println(F("Send 'W' to calibrate white card, 'K' to calibrate black target"));
  Serial.println(F("Press SET STANDARD button to save reference color"));
  Serial.println(F("Press CHECK COLOR button to inspect (5s measurement)"));

  oledMessage("Ready!", "W/K = calibrate", "Btn1/Btn2 = use");
}

void loop() {
  // ---- 1) จัดการคำสั่ง Calibration ผ่าน Serial ----
  handleSerialCalibration();

  // ---- 2) เช็คสภาพแวดล้อมเป็นระยะ (ไม่บล็อกการทำงานอื่น) ----
  if (millis() - lastEnvCheckMs >= ENV_CHECK_INTERVAL_MS) {
    lastEnvCheckMs = millis();
    checkEnvironment();
  }

  // ---- 3) จัดการ flag จากปุ่มกด (ตั้งค่าจาก ISR) ----
  if (requestSetStandard && appState == STATE_IDLE) {
    noInterrupts(); requestSetStandard = false; interrupts();
    if (!calib.calibrated) {
      Serial.println(F("[WARN] Not calibrated yet - readings may be inaccurate"));
    }
    startMeasurement(STATE_MEASURE_STANDARD);
  }

  if (requestCheckColor && appState == STATE_IDLE) {
    noInterrupts(); requestCheckColor = false; interrupts();
    if (!hasStandard) {
      Serial.println(F("[ERROR] No standard color saved yet. Press SET STANDARD first."));
      oledMessage("NO STANDARD", "Set standard", "color first");
      setStatusLed(false, false, true);
    } else {
      startMeasurement(STATE_MEASURE_CHECK);
    }
  }

  // ---- 4) State machine การวัดสี (non-blocking) ----
  if (appState == STATE_MEASURE_STANDARD || appState == STATE_MEASURE_CHECK) {
    updateMeasurement();
  }
}

// =====================================================================
// การวัดสี (non-blocking state machine)
// =====================================================================
void startMeasurement(AppState target) {
  appState = target;
  measureStartMs = millis();
  sumRed = sumGreen = sumBlue = 0;
  sampleCount = 0;
  lastBlinkMs = millis();
  lastPrintMs = millis();
  yellowState = false;
  setStatusLed(false, true, false); // ไฟเหลืองเริ่มติด (จะกระพริบใน updateMeasurement)

  String label = (target == STATE_MEASURE_STANDARD) ? "SETTING STANDARD" : "CHECKING COLOR";
  Serial.println("=== " + label + " (5s) ===");
  oledMessage(label, "Measuring...", "");
}

void updateMeasurement() {
  unsigned long elapsed = millis() - measureStartMs;

  // กระพริบไฟเหลืองระหว่างวัด (non-blocking)
  if (millis() - lastBlinkMs >= BLINK_INTERVAL_MS) {
    lastBlinkMs = millis();
    yellowState = !yellowState;
    digitalWrite(LED_YELLOW, yellowState ? HIGH : LOW);
  }

  // เก็บตัวอย่างค่าสี (pulseIn บล็อกสั้นๆ ไม่เกิน 3x50ms ต่อรอบ ยอมรับได้)
  unsigned long r = readColorFrequency(LOW, LOW);
  unsigned long g = readColorFrequency(HIGH, HIGH);
  unsigned long b = readColorFrequency(LOW, HIGH);
  if (r > 0 && g > 0 && b > 0) {
    sumRed += r; sumGreen += g; sumBlue += b;
    sampleCount++;
  }

  // พิมพ์ผลระหว่างวัดทุก 1 วินาที ตามที่ขอ
  if (millis() - lastPrintMs >= PRINT_INTERVAL_MS) {
    lastPrintMs = millis();
    Serial.print("t=" + String(elapsed / 1000.0f, 1) + "s  ");
    Serial.print("raw R:" + String(r) + " G:" + String(g) + " B:" + String(b));
    Serial.println("  samples=" + String(sampleCount));
  }

  // ครบเวลาที่กำหนดแล้ว -> สรุปผล
  if (elapsed >= MEASURE_DURATION_MS) {
    finishMeasurement();
  }
}

void finishMeasurement() {
  digitalWrite(LED_YELLOW, LOW);

  if (sampleCount == 0) {
    Serial.println(F("[ERROR] No valid samples - check TCS3200 wiring"));
    oledMessage("MEASURE FAIL", "No signal from", "TCS3200");
    setStatusLed(false, false, true);
    appState = STATE_IDLE;
    return;
  }

  unsigned long avgR = sumRed / sampleCount;
  unsigned long avgG = sumGreen / sampleCount;
  unsigned long avgB = sumBlue / sampleCount;

  RGBColor rgb = frequencyToRGB(avgR, avgG, avgB);
  LabColor lab = rgbToLab(rgb);
  showRgbOnLed(rgb);

  if (appState == STATE_MEASURE_STANDARD) {
    standardRgb = rgb;
    standardLab = lab;
    hasStandard = true;
    setStatusLed(true, false, false);

    Serial.println("STANDARD SAVED - RGB(" + String(rgb.r) + "," + String(rgb.g) + "," + String(rgb.b) + ")"
        + " Lab(" + String(lab.L, 1) + "," + String(lab.a, 1) + "," + String(lab.b, 1) + ")");
    oledMessage("STANDARD SAVED",
        "RGB:" + String(rgb.r) + "," + String(rgb.g) + "," + String(rgb.b),
        "L:" + String(lab.L, 0) + " a:" + String(lab.a, 0) + " b:" + String(lab.b, 0));

  } else { // STATE_MEASURE_CHECK
    float deltaE = calculateDeltaE(lab, standardLab);
    bool pass = deltaE <= DELTA_E_THRESHOLD;
    setStatusLed(pass, false, !pass);

    Serial.println("RESULT: RGB(" + String(rgb.r) + "," + String(rgb.g) + "," + String(rgb.b) + ")"
        + " Lab(" + String(lab.L, 1) + "," + String(lab.a, 1) + "," + String(lab.b, 1) + ")"
        + "  dE=" + String(deltaE, 2) + "  " + (pass ? "PASS" : "FAIL"));

    oledMessage(pass ? "PASS" : "FAIL",
        "dE=" + String(deltaE, 2) + " (max " + String(DELTA_E_THRESHOLD, 1) + ")",
        "RGB:" + String(rgb.r) + "," + String(rgb.g) + "," + String(rgb.b));
  }

  appState = STATE_IDLE;
}

// =====================================================================
// TCS3200: อ่านความถี่พัลส์ 1 ช่องสี
// =====================================================================
unsigned long readColorFrequency(bool s2State, bool s3State) {
  digitalWrite(S2, s2State);
  digitalWrite(S3, s3State);
  delayMicroseconds(200); // หน่วงสั้นๆ ให้ filter เปลี่ยนโหมด (เร็วกว่า delay(10) แบบเดิม)
  return pulseIn(Out, LOW, PULSE_TIMEOUT_US);
}

// =====================================================================
// แปลงความถี่ดิบ -> RGB 0-255 โดยใช้ค่า Calibration (White/Black)
// ถ้ายังไม่ calibrate จะ map แบบหยาบด้วยค่า default (ไม่แม่นยำ)
// =====================================================================
RGBColor frequencyToRGB(unsigned long r, unsigned long g, unsigned long b) {
  RGBColor out;
  if (calib.calibrated) {
    // ค่าความถี่ "ต่ำ" ของ TCS3200 = แสงเข้ม (สีสว่าง), ความถี่ "สูง" = แสงอ่อน (สีเข้ม)
    // จึงต้อง map แบบกลับด้าน (low freq -> 255, high freq -> 0)
    out.r = constrain(map(r, calib.redMax, calib.redMin, 0, 255), 0, 255);
    out.g = constrain(map(g, calib.greenMax, calib.greenMin, 0, 255), 0, 255);
    out.b = constrain(map(b, calib.blueMax, calib.blueMin, 0, 255), 0, 255);
  } else {
    // ค่า default หยาบๆ เผื่อยังไม่ calibrate (ควร calibrate ก่อนใช้งานจริงเสมอ)
    out.r = constrain(map(r, 3000, 300, 0, 255), 0, 255);
    out.g = constrain(map(g, 3000, 300, 0, 255), 0, 255);
    out.b = constrain(map(b, 3000, 300, 0, 255), 0, 255);
  }
  return out;
}

// =====================================================================
// RGB (0-255) -> CIE XYZ -> CIE L*a*b*
// อ้างอิงสูตรมาตรฐาน sRGB -> XYZ (D65 illuminant) -> Lab
// =====================================================================
LabColor rgbToLab(const RGBColor &rgb) {
  // 1) sRGB (0-255) -> linear RGB (0-1) พร้อม gamma correction
  float rf = rgb.r / 255.0f, gf = rgb.g / 255.0f, bf = rgb.b / 255.0f;
  auto gammaCorrect = [](float c) {
    return (c > 0.04045f) ? powf((c + 0.055f) / 1.055f, 2.4f) : (c / 12.92f);
  };
  rf = gammaCorrect(rf);
  gf = gammaCorrect(gf);
  bf = gammaCorrect(bf);

  // 2) linear RGB -> XYZ (เมทริกซ์มาตรฐาน sRGB, D65)
  float X = rf * 0.4124f + gf * 0.3576f + bf * 0.1805f;
  float Y = rf * 0.2126f + gf * 0.7152f + bf * 0.0722f;
  float Z = rf * 0.0193f + gf * 0.1192f + bf * 0.9505f;

  // 3) XYZ -> Lab (ใช้ white reference D65: Xn=0.95047, Yn=1.0, Zn=1.08883)
  const float Xn = 0.95047f, Yn = 1.0f, Zn = 1.08883f;
  auto f = [](float t) {
    const float delta = 6.0f / 29.0f;
    return (t > delta * delta * delta) ? cbrtf(t) : (t / (3 * delta * delta) + 4.0f / 29.0f);
  };
  float fx = f(X / Xn), fy = f(Y / Yn), fz = f(Z / Zn);

  LabColor lab;
  lab.L = 116.0f * fy - 16.0f;
  lab.a = 500.0f * (fx - fy);
  lab.b = 200.0f * (fy - fz);
  return lab;
}

// =====================================================================
// Delta E (CIE76) = ระยะห่างแบบ Euclidean ใน Lab space
// =====================================================================
float calculateDeltaE(const LabColor &c1, const LabColor &c2) {
  float dL = c1.L - c2.L;
  float da = c1.a - c2.a;
  float db = c1.b - c2.b;
  return sqrtf(dL * dL + da * da + db * db);
}

// =====================================================================
// Calibration ผ่าน Serial: พิมพ์ 'W' = ขาว, 'K' = ดำ
// =====================================================================
void handleSerialCalibration() {
  if (!Serial.available()) return;
  char c = Serial.read();

  if (c == 'W' || c == 'w') {
    Serial.println(F("Calibrating WHITE... place white card, reading 1s"));
    unsigned long sr = 0, sg = 0, sb = 0;
    for (int i = 0; i < 10; i++) {
      sr += readColorFrequency(LOW, LOW);
      sg += readColorFrequency(HIGH, HIGH);
      sb += readColorFrequency(LOW, HIGH);
    }
    calib.redMin = sr / 10;   // ขาว = ความถี่ต่ำสุด (แสงเข้มที่สุด)
    calib.greenMin = sg / 10;
    calib.blueMin = sb / 10;
    Serial.println("White calib: R=" + String(calib.redMin) + " G=" + String(calib.greenMin) + " B=" + String(calib.blueMin));
    if (calib.redMax > 0) calib.calibrated = true;

  } else if (c == 'K' || c == 'k') {
    Serial.println(F("Calibrating BLACK... place black target, reading 1s"));
    unsigned long sr = 0, sg = 0, sb = 0;
    for (int i = 0; i < 10; i++) {
      sr += readColorFrequency(LOW, LOW);
      sg += readColorFrequency(HIGH, HIGH);
      sb += readColorFrequency(LOW, HIGH);
    }
    calib.redMax = sr / 10;   // ดำ = ความถี่สูงสุด (แสงอ่อนที่สุด)
    calib.greenMax = sg / 10;
    calib.blueMax = sb / 10;
    Serial.println("Black calib: R=" + String(calib.redMax) + " G=" + String(calib.greenMax) + " B=" + String(calib.blueMax));
    if (calib.redMin > 0) calib.calibrated = true;
  }

  if (calib.calibrated) {
    Serial.println(F(">>> Calibration COMPLETE <<<"));
    oledMessage("Calibrated!", "Ready to use", "");
  }
}

// =====================================================================
// DHT11: เช็คสภาพแวดล้อม (แจ้งเตือนแต่ไม่หยุดระบบ)
// =====================================================================
void checkEnvironment() {
  float t = dht.readTemperature();
  float h = dht.readHumidity();
  if (isnan(t) || isnan(h)) return; // อ่านไม่ได้รอบนี้ ข้ามไปเฉยๆ ไม่ฟ้อง error รัวๆ

  bool tempOK = (t >= TEMP_MIN && t <= TEMP_MAX);
  bool humOK  = (h >= HUM_MIN && h <= HUM_MAX);

  if (!tempOK || !humOK) {
    Serial.println("[ENV WARNING] Temp=" + String(t, 1) + "C Hum=" + String(h, 0)
        + "% - outside ideal range (T:" + String(TEMP_MIN, 0) + "-" + String(TEMP_MAX, 0)
        + " H:" + String(HUM_MIN, 0) + "-" + String(HUM_MAX, 0) + "). Measurement continues.");
  }
}

// =====================================================================
// LED helpers
// =====================================================================
void setStatusLed(bool green, bool yellow, bool red) {
  digitalWrite(LED_GREEN, green ? HIGH : LOW);
  digitalWrite(LED_YELLOW, yellow ? HIGH : LOW);
  digitalWrite(LED_RED, red ? HIGH : LOW);
}

void showRgbOnLed(const RGBColor &rgb) {
  analogWrite(LED_R, rgb.r); // ESP32 Arduino core: analogWrite ใช้ PWM ให้อัตโนมัติ
  analogWrite(LED_G, rgb.g);
  analogWrite(LED_B, rgb.b);
}

// =====================================================================
// OLED helper
// =====================================================================
void oledMessage(const String &line1, const String &line2, const String &line3) {
  if (!oledOK) return;
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println(line1);
  display.println();
  if (line2.length() > 0) display.println(line2);
  if (line3.length() > 0) display.println(line3);
  display.display();
}