#include <Arduino.h>

// set up the DHT sensor
#include "DHT.h"
#define DHTTYPE DHT11   // DHT 11
const int DHTPIN = 4; // Pin which is connected to the DHT sensor
DHT dht(DHTPIN, DHTTYPE); 

//set up ssd1306 OLED display
#include <Adafruit_SSD1306.h>
#include <Adafruit_GFX.h>
#include <Wire.h>
const int SCREEN_WIDTH = 128; // OLED display width, in pixels
const int SCREEN_HEIGHT = 64; // OLED display height, in pixels
const int OLED_RESET = -1;    // Reset pin # (or -1 if sharing Arduino reset pin)
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

void check_equipment();

void setup() {
  Serial.begin(115200);
  dht.begin();
  Wire.begin(21,22); // Initialize I2C with SDA on GPIO 21 and SCL on GPIO 22
  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);  // initialize with the I2C addr 0x3C (for the 128x64)

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

}