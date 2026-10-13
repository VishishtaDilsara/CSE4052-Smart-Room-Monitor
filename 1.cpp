#include <Wire.h>
#include <U8g2lib.h>
#include <DHT.h>

// ------------------------------
// Pin configuration
// ------------------------------
#define DHTPIN 32
#define DHTTYPE DHT11

#define OLED_SDA 27
#define OLED_SCL 25

// ------------------------------
// DHT11 sensor
// ------------------------------
DHT dht(DHTPIN, DHTTYPE);

// ------------------------------
// OLED display
// Assumes 128x64 SH1106 I2C OLED
// ------------------------------
U8G2_SH1106_128X64_NONAME_F_HW_I2C oled(
U8G2_R0,
U8X8_PIN_NONE
);

// ------------------------------
// Setup
// ------------------------------
void setup() {
Serial.begin(115200);

// Initialize I2C with custom pins
Wire.begin(OLED_SDA, OLED_SCL);

// Initialize sensor and display
dht.begin();
oled.begin();

// Startup screen
oled.clearBuffer();
oled.setFont(u8g2_font_6x12_tf);
oled.drawStr(12, 25, "SMART ROOM MONITOR");
oled.drawStr(30, 45, "Initializing...");
oled.sendBuffer();

Serial.println("Smart Room Monitor Started");

delay(2000);
}

// ------------------------------
// Main loop
// ------------------------------
void loop() {
float temperature = dht.readTemperature();
float humidity = dht.readHumidity();

oled.clearBuffer();

// Header
oled.setFont(u8g2_font_6x12_tf);
oled.drawStr(15, 12, "SMART ROOM MONITOR");
oled.drawHLine(0, 17, 128);

// Handle sensor errors
if (isnan(temperature) || isnan(humidity)) {
oled.drawStr(5, 36, "Sensor read failed!");
oled.drawStr(5, 52, "Check DHT11 wiring");

Serial.println("DHT11 reading failed.");

} else {
// Format readings
char tempText[24];
char humidityText[24];

snprintf(
tempText,
sizeof(tempText),
"Temp: %.1f C",
temperature
);

snprintf(
humidityText,
sizeof(humidityText),
"Humidity: %.1f %%",
humidity
);

// Display temperature
oled.setFont(u8g2_font_ncenB14_tr);
oled.drawStr(5, 38, tempText);

// Display humidity
oled.setFont(u8g2_font_6x12_tf);
oled.drawStr(5, 57, humidityText);

// Print readings to Serial Monitor
Serial.print("Temperature: ");
Serial.print(temperature);
Serial.print(" C | Humidity: ");
Serial.print(humidity);
Serial.println(" % RH");

}

// Send buffer to OLED
oled.sendBuffer();

// DHT11 reading interval
delay(2000);
}