// set up the DHT sensor
#include "DHT.h"
#define DHTTYPE DHT11   // DHT 11
const int DHTPIN = 4; // Pin which is connected to the DHT sensor
DHT dht(DHTPIN, DHTTYPE);  // Initialize DHT sensor for normal 16mhz Arduino

//set up ssd1306 OLED display
#include <Adafruit_SSD1306.h>
#include <Adafruit_GFX.h>
#include <Wire.h>
const int SCREEN_WIDTH = 128; // OLED display width, in pixels
const int SCREEN_HEIGHT = 64; // OLED display height, in pixels
const int OLED_RESET = -1;    // Reset pin # (or -1 if sharing Arduino reset pin)
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET); // Create an instance of the SSD1306 display

//set up led
const int LED_RED = 27;
const int LED_GREEN = 13;
const int LED_YELLOW = 14;

//set up RGB LED
const int LED_R = 5;
const int LED_G = 23;
const int LED_B = 15;

//set up button
const int button_set_standard = 18;
const int button_check_color = 19;

//set tcs3200 color sensor
const int S0 = 32;
const int S1 = 33;
const int S2 = 25;
const int S3 = 26;
const int Out = 34;
const unsigned long PULSE_TIMEOUT_US = 50000UL; // [เพิ่ม] กัน pulseIn ค้างถ้าไม่มีสัญญาณ

bool oledOK = false;

void check_equipment();
void showStatus(const String &label, bool ok, const String &extra = "");
unsigned long readColorFrequency(bool s2State, bool s3State);

void setup() {
  Serial.begin(115200);
  dht.begin();
  Wire.begin(21, 22); // Initialize I2C with SDA on GPIO 21 and SCL on GPIO 22
  Wire.setTimeOut(1000); // Set I2C timeout to 1000 milliseconds

  // set up pins
  pinMode(LED_RED, OUTPUT);
  pinMode(LED_GREEN, OUTPUT);
  pinMode(LED_YELLOW, OUTPUT);
  pinMode(LED_R, OUTPUT);
  pinMode(LED_G, OUTPUT);
  pinMode(LED_B, OUTPUT);
  pinMode(button_set_standard, INPUT_PULLUP);
  pinMode(button_check_color, INPUT_PULLUP);
  pinMode(S0, OUTPUT);
  pinMode(S1, OUTPUT);
  pinMode(S2, OUTPUT);
  pinMode(S3, OUTPUT);
  pinMode(Out, INPUT);

  // set frequency scaling to 20% for TCS3200
  digitalWrite(S0, HIGH);
  digitalWrite(S1, LOW);

  check_equipment();

  if (oledOK) {
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    display.println("Ready!");
    display.display();
  }
}

void loop() {

}

// helper: แสดงผลทั้ง Serial และ OLED (ถ้าจอพร้อม) ในบรรทัดเดียวกัน
void showStatus(const String &label, bool ok, const String &extra) {
  String line = label + ": " + (ok ? "OK" : "FAIL");
  if (extra.length() > 0) line += " " + extra;
  Serial.println(line);

  if (oledOK) {
    display.println(line);
    display.display();
  }
}

// [เพิ่ม] อ่านค่าความถี่พัลส์ 1 ช่องสีจาก TCS3200
unsigned long readColorFrequency(bool s2State, bool s3State) {
  digitalWrite(S2, s2State);
  digitalWrite(S3, s3State);
  delay(10); // รอฟิลเตอร์เปลี่ยนโหมดเสร็จ
  return pulseIn(Out, LOW, PULSE_TIMEOUT_US); // 0 = ไม่พบสัญญาณ
}

void check_equipment() {
  // ----- OLED -----
  oledOK = display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  if (oledOK) {
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    display.println("Equipment Check");
    display.display();
  } else {
    Serial.println("SSD1306 allocation failed - continuing without OLED");
  }

  // ----- 1) เช็ค DHT11 -----
  float h = dht.readHumidity();
  float t = dht.readTemperature();
  bool dhtOK = !(isnan(h) || isnan(t));
  showStatus("DHT11", dhtOK,
      dhtOK ? (String(t, 1) + "C " + String(h, 0) + "%") : "");

  // ----- 2) เช็ค LED สถานะ + RGB LED -----
  digitalWrite(LED_RED, HIGH);    delay(300); digitalWrite(LED_RED, LOW);
  digitalWrite(LED_YELLOW, HIGH); delay(300); digitalWrite(LED_YELLOW, LOW);
  digitalWrite(LED_GREEN, HIGH);  delay(300); digitalWrite(LED_GREEN, LOW);
  digitalWrite(LED_R, HIGH);      delay(300); digitalWrite(LED_R, LOW);
  digitalWrite(LED_G, HIGH);      delay(300); digitalWrite(LED_G, LOW);
  digitalWrite(LED_B, HIGH);      delay(300); digitalWrite(LED_B, LOW);
  showStatus("LEDs", true, "check by eye");

  // ----- 3) เช็ค TCS3200 -----
  unsigned long red   = readColorFrequency(LOW, LOW);
  unsigned long green = readColorFrequency(HIGH, HIGH);
  unsigned long blue  = readColorFrequency(LOW, HIGH);
  bool tcsOK = (red > 0 || green > 0 || blue > 0);
  showStatus("TCS3200", tcsOK,
      "R:" + String(red) + " G:" + String(green) + " B:" + String(blue));

  // ----- 4) เช็คปุ่มกด -----
  Serial.println("Press either button within 5s...");
  if (oledOK) {
    display.println("Press a button");
    display.println("(5s timeout)");
    display.display();
  }

  const unsigned long BUTTON_TIMEOUT_MS = 5000;
  unsigned long startTime = millis();
  bool buttonPressed = false;
  String whichButton = "none";

  while (millis() - startTime < BUTTON_TIMEOUT_MS) {
    if (digitalRead(button_set_standard) == LOW) {
      buttonPressed = true;
      whichButton = "Set standard";
      break;
    }
    if (digitalRead(button_check_color) == LOW) {
      buttonPressed = true;
      whichButton = "Measure color";
      break;
    }
    delay(10);
  }

  showStatus("Button", buttonPressed,
      buttonPressed ? ("(" + whichButton + ")") : "(timeout, no press)");

  Serial.println("=== Equipment check done ===");
  if (oledOK) {
    display.println("--- Done ---");
    display.display();
  }
}