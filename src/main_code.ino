/***************************************************
   IoT EV Diagnostic Tool - ESP32 + Dual ILI9341 TFT
   TFT1: Parameters
   TFT2: Alerts + Driver Behavior
***************************************************/

#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>
#include <DHT.h>
#include <EEPROM.h>

// ----------- TFT CONFIG -------------
#define TFT1_CS   5
#define TFT1_RST  4
#define TFT1_DC   2

#define TFT2_CS   21
#define TFT2_RST  22
#define TFT2_DC   26

#define TFT_MOSI 23
#define TFT_CLK  18
#define TFT_MISO 19  // not used

Adafruit_ILI9341 tft1 = Adafruit_ILI9341(TFT1_CS, TFT1_DC, TFT1_RST);
Adafruit_ILI9341 tft2 = Adafruit_ILI9341(TFT2_CS, TFT2_DC, TFT2_RST);

// ----------- SENSOR CONFIG -------------
#define VOLTAGE_PIN 35
#define CURRENT_PIN 34
#define DHTPIN 27
#define DHTTYPE DHT22

// Voltage divider values
float R1 = 30000.0, R2 = 7500.0, refVoltage = 3.3;
int adcMax = 4095;

// Battery thresholds
float lowVoltage = 10.5, highVoltage = 12.6;

// Battery Capacity
float ratedCapacity = 2000.0, measuredCapacity = 0.0;
int cycleCount = 0, lastSOC = 100;
float batteryHealth = 100.0;

// EEPROM size
#define EEPROM_SIZE 64

// Driver Behavior
float lastCurrent = 0;
int harshAccelCount = 0;
int harshBrakeCount = 0;
int overspeedCount = 0;
unsigned long lastDriverUpdate = 0;

DHT dht(DHTPIN, DHTTYPE);
unsigned long lastUpdate = 0;

void setup() {
  EEPROM.begin(EEPROM_SIZE);
  cycleCount = EEPROM.read(0);
  batteryHealth = EEPROM.read(1);
  if (batteryHealth == 255) batteryHealth = 100;

  dht.begin();

  // Init TFTs
  tft1.begin();
  tft2.begin();
  tft1.setRotation(1);
  tft2.setRotation(1);

  // Startup screens
  tft1.fillScreen(ILI9341_BLACK);
  tft1.setTextColor(ILI9341_GREEN, ILI9341_BLACK);
  tft1.setTextSize(2);
  tft1.println("EV Diagnostics");

  tft2.fillScreen(ILI9341_BLACK);
  tft2.setTextColor(ILI9341_YELLOW, ILI9341_BLACK);
  tft2.setTextSize(2);
  tft2.println("Alerts Display");

  delay(2000);
  tft1.fillScreen(ILI9341_BLACK);
  tft2.fillScreen(ILI9341_BLACK);
}

float readBatteryVoltage() {
  int adcValue = analogRead(VOLTAGE_PIN);
  float vOut = (adcValue * refVoltage) / adcMax;
  return vOut / (R2 / (R1 + R2));
}

float readBatteryCurrent() {
  int adcValue = analogRead(CURRENT_PIN);
  float voltage = (adcValue * refVoltage) / adcMax;
  return (voltage / refVoltage) * 20.0; // 0–20A simulated
}

int estimateSOC(float voltage) {
  if (voltage >= highVoltage) return 100;
  if (voltage <= lowVoltage) return 0;
  return (int)(((voltage - lowVoltage) / (highVoltage - lowVoltage)) * 100);
}

void saveToEEPROM() {
  EEPROM.write(0, cycleCount);
  EEPROM.write(1, (int)batteryHealth);
  EEPROM.commit();
}

void displayParameters(float voltage, float current, float tempC, float humid, int soc) {
  tft1.fillScreen(ILI9341_BLACK);
  tft1.setCursor(10, 20);  tft1.print("Voltage: "); tft1.print(voltage); tft1.println(" V");
  tft1.setCursor(10, 50);  tft1.print("Current: "); tft1.print(current); tft1.println(" A");
  tft1.setCursor(10, 80);  tft1.print("Temp: "); tft1.print(tempC); tft1.println(" C");
  tft1.setCursor(10, 110); tft1.print("Humidity: "); tft1.print(humid); tft1.println(" %");
  tft1.setCursor(10, 140); tft1.print("SOC: "); tft1.print(soc); tft1.println(" %");
  tft1.setCursor(10, 170); tft1.print("Cycles: "); tft1.println(cycleCount);
  tft1.setCursor(10, 200); tft1.print("Health: "); tft1.print(batteryHealth); tft1.println(" %");
}

void displayAlerts(float voltage, float tempC, float humid) {
  tft2.fillScreen(ILI9341_BLACK);
  tft2.setTextSize(2);
  tft2.setCursor(10, 30);

  if (tempC > 45) {
    tft2.setTextColor(ILI9341_RED, ILI9341_BLACK);
    tft2.println("ALERT: Overheat!");
  }
  if (voltage < lowVoltage) {
    tft2.setTextColor(ILI9341_RED, ILI9341_BLACK);
    tft2.println("ALERT: Low Voltage!");
  }
  if (humid > 90) {
    tft2.setTextColor(ILI9341_RED, ILI9341_BLACK);
    tft2.println("ALERT: High Humidity!");
  }

  // If no alerts
  if (tempC <= 45 && voltage >= lowVoltage && humid <= 90) {
    tft2.setTextColor(ILI9341_GREEN, ILI9341_BLACK);
    tft2.println("All Systems Normal");
  }

  // Show driver behavior
  tft2.setCursor(10, 120);
  tft2.setTextColor(ILI9341_YELLOW, ILI9341_BLACK);
  tft2.println("Driver Behavior:");
  tft2.setCursor(10, 150); tft2.print("Accel: "); tft2.println(harshAccelCount);
  tft2.setCursor(10, 170); tft2.print("Brake: "); tft2.println(harshBrakeCount);
  tft2.setCursor(10, 190); tft2.print("Overspeed: "); tft2.println(overspeedCount);
}

void detectDriverBehavior(float current, float voltage) {
  unsigned long now = millis();
  if (now - lastDriverUpdate < 1000) return; // update every sec
  lastDriverUpdate = now;

  float deltaCurrent = current - lastCurrent;
  lastCurrent = current;

  if (deltaCurrent > 5.0) harshAccelCount++;
  if (deltaCurrent < -5.0) harshBrakeCount++;

  // Fake overspeed detection (SOC + voltage relation)
  if (voltage > 12.5) overspeedCount++;
}

void loop() {
  float voltage = readBatteryVoltage();
  float current = readBatteryCurrent();
  float tempC = dht.readTemperature();
  float humid = dht.readHumidity();
  int soc = estimateSOC(voltage);

  unsigned long now = millis();
  float dt = (now - lastUpdate) / 3600000.0;
  lastUpdate = now;
  measuredCapacity += current * 1000 * dt;

  // Cycle detection
  if (lastSOC > 90 && soc < 20) cycleCount++;
  if (lastSOC < 20 && soc > 90) {
    batteryHealth -= 0.05;
    if (batteryHealth < 60) batteryHealth = 60;
    saveToEEPROM();
    measuredCapacity = 0;
  }
  lastSOC = soc;

  // Driver behavior check
  detectDriverBehavior(current, voltage);

  // Update displays
  displayParameters(voltage, current, tempC, humid, soc);
  displayAlerts(voltage, tempC, humid);

  delay(2000);
}