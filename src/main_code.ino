

#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>
#include <DHT.h>
#include <EEPROM.h>
#include <MPU6050.h>

// ----------- TFT CONFIG -------------
#define TFT1_CS   5
#define TFT1_RST  4
#define TFT1_DC   2

#define TFT2_CS   21
#define TFT2_RST  22
#define TFT2_DC   26

#define TFT_MOSI 23
#define TFT_CLK  18
#define TFT_MISO 19

Adafruit_ILI9341 tft1 = Adafruit_ILI9341(TFT1_CS, TFT1_DC, TFT1_RST);
Adafruit_ILI9341 tft2 = Adafruit_ILI9341(TFT2_CS, TFT2_DC, TFT2_RST);

// ----------- SENSOR CONFIG -------------
#define VOLTAGE_PIN 35
#define CURRENT_PIN 34
#define DHTPIN 27
#define DHTTYPE DHT22

// Voltage divider
float R1 = 30000.0, R2 = 7500.0, refVoltage = 3.3;
int adcMax = 4095;

// Battery thresholds
float lowVoltage = 10.5, highVoltage = 12.6;
float highCurrentThreshold = 10.0; // Ampere limit for high usage alert

// Battery Capacity
float ratedCapacity = 2000.0, measuredCapacity = 0.0;
int cycleCount = 0, lastSOC = 100;
float batteryHealth = 100.0;

// EEPROM
#define EEPROM_SIZE 64

DHT dht(DHTPIN, DHTTYPE);
unsigned long lastUpdate = 0;

// MPU6050
MPU6050 mpu;
int16_t ax, ay, az;   // FIX: must be int16_t
float accelThreshold = 2.0; // g-force threshold for rash driving
bool rashDriving = false;
bool highCurrent = false;

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

  // Init MPU6050
  Wire.begin(33, 32); 
  mpu.initialize();

  // Startup screens
  tft1.fillScreen(ILI9341_BLACK);
  tft1.setTextColor(ILI9341_GREEN, ILI9341_BLACK);
  tft1.setTextSize(2);
  tft1.println("EV Diagnostics");

  tft2.fillScreen(ILI9341_BLACK);
  tft2.setTextColor(ILI9341_YELLOW, ILI9341_BLACK);
  tft2.setTextSize(2);
  tft2.println("Alerts Display");

  delay(1000);
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
  float current = (voltage / refVoltage) * 20.0; // simulated
  highCurrent = (current > highCurrentThreshold);
  return current;
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

void checkRashDriving() {
  mpu.getAcceleration(&ax, &ay, &az);
  // Convert raw readings to 'g'
  float totalAccel = sqrt((float)ax * ax + (float)ay * ay + (float)az * az) / 16384.0;
  rashDriving = (totalAccel > accelThreshold);
}

void displayParameters(float voltage, float current, float tempC, float humid, int soc) {
  tft1.fillScreen(ILI9341_BLACK);
  tft1.setCursor(10, 20);  tft1.printf("Voltage: %.2f V", voltage);
  tft1.setCursor(10, 50);  tft1.printf("Current: %.2f A", current);
  tft1.setCursor(10, 80);  tft1.printf("Temp: %.1f C", tempC);
  tft1.setCursor(10, 110); tft1.printf("Humidity: %.1f %%", humid);
  tft1.setCursor(10, 140); tft1.printf("SOC: %d %%", soc);
  tft1.setCursor(10, 170); tft1.printf("Cycles: %d", cycleCount);
  tft1.setCursor(10, 200); tft1.printf("Health: %.1f %%", batteryHealth);
}

void displayAlerts(float voltage, float tempC, float humid) {
  tft2.fillScreen(ILI9341_BLACK);
  tft2.setTextSize(2);
  tft2.setCursor(10, 30);

  bool alertShown = false;

  if (tempC > 45) {
    tft2.setTextColor(ILI9341_RED, ILI9341_BLACK);
    tft2.println("ALERT: Overheat!");
    alertShown = true;
  }
  if (voltage < lowVoltage) {
    tft2.setTextColor(ILI9341_RED, ILI9341_BLACK);
    tft2.println("ALERT: Low Voltage!");
    alertShown = true;
  }
  if (humid > 90) {
    tft2.setTextColor(ILI9341_RED, ILI9341_BLACK);
    tft2.println("ALERT: High Humidity!");
    alertShown = true;
  }
  if (rashDriving) {
    tft2.setTextColor(ILI9341_RED, ILI9341_BLACK);
    tft2.println("ALERT: Rash Driving!");
    alertShown = true;
  }
  if (highCurrent) {
    tft2.setTextColor(ILI9341_RED, ILI9341_BLACK);
    tft2.println("ALERT: High Batt Usage!");
    alertShown = true;
  }

  if (!alertShown) {
    tft2.setTextColor(ILI9341_GREEN, ILI9341_BLACK);
    tft2.println("All Systems Normal");
  }
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

  if (lastSOC > 90 && soc < 20) cycleCount++;
  if (lastSOC < 20 && soc > 90) {
    batteryHealth -= 0.05;
    if (batteryHealth < 60) batteryHealth = 60;
    saveToEEPROM();
    measuredCapacity = 0;
  }
  lastSOC = soc;

  checkRashDriving();
  displayParameters(voltage, current, tempC, humid, soc);
  displayAlerts(voltage, tempC, humid);

  delay(2000);
}
