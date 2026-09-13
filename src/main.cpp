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

void check_equipment();

void setup() {
  Serial.begin(115200);
  dht.begin();
  Wire.begin(21,22); // Initialize I2C with SDA on GPIO 21 and SCL on GPIO 22
  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);  // initialize with the I2C addr 0x3C (for the 128x64)

  // set up the LED pins
  pinMode(LED_RED, OUTPUT);
  pinMode(LED_GREEN, OUTPUT);
  pinMode(LED_YELLOW, OUTPUT);
  pinMode(LED_R, OUTPUT);
  pinMode(LED_G, OUTPUT);
  pinMode(LED_B, OUTPUT);

  // check the equipment
  check_equipment();

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println("Ready!");
  display.display();
}

void loop() {
  float h = dht.readHumidity();
  float t = dht.readTemperature();
  Serial.print("Humidity: ");
  Serial.print(h);
  Serial.print(" %\t");
  Serial.print("Temperature: ");
  Serial.println(t);
  delay(2000); // Wait a few seconds between measurements.
}


void check_equipment() {
  // check the DHT sensor
  float h = dht.readHumidity();
  float t = dht.readTemperature();

  if (isnan(h) || isnan(t)) {
    Serial.println("Failed to read from DHT sensor!");
    return;
  }
  Serial.println("DHT11: OK");

  // check the OLED display
  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("SSD1306 allocation failed");
    for(;;); // Don't proceed, loop forever
  }
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println("OLED: OK");
  display.display();

  // check the LEDs
  digitalWrite(LED_RED, HIGH);
  delay(500);
  digitalWrite(LED_RED, LOW);
  digitalWrite(LED_YELLOW, HIGH);
  delay(500);
  digitalWrite(LED_YELLOW, LOW);
  digitalWrite(LED_GREEN, HIGH);
  delay(500);
  digitalWrite(LED_GREEN, LOW);
  digitalWrite(LED_R, HIGH);
  delay(500);
  digitalWrite(LED_R, LOW);
  digitalWrite(LED_G, HIGH);
  delay(500);
  digitalWrite(LED_G, LOW);
  digitalWrite(LED_B, HIGH);
  delay(500);
  digitalWrite(LED_B, LOW);
  Serial.println("LEDs: OK(ถ้าไฟติดและดับตามลำดับ)");

}