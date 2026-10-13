
    
  
#include <Arduino.h>
#include <Wire.h>
#include <U8g2lib.h>
#include <DHT.h>
#include <Preferences.h>
#include <WiFi.h>
#include <WebServer.h>
#include <WebSocketsServer.h>
#include <ArduinoJson.h>
#include <math.h>

// ==================================================
// Hardware
// ==================================================
#define DHTPIN 32
#define DHTTYPE DHT11
#define OLED_SDA 27
#define OLED_SCL 25
#define CONFIG_BTN 33

#define DEFAULT_INTERVAL_MS 2000
#define MIN_INTERVAL_MS 2000
#define MAX_INTERVAL_MS 60000
#define DEBOUNCE_US 200000

// ==================================================
// Hardware objects
// ==================================================
DHT dht(DHTPIN, DHTTYPE);

U8G2_SH1106_128X64_NONAME_F_HW_I2C oled(
U8G2_R0,
U8X8_PIN_NONE
);

Preferences preferences;
WebServer webServer(80);
WebSocketsServer webSocket(81);

hw_timer_t *samplingTimer = nullptr;

QueueHandle_t sensorQueue = nullptr;
SemaphoreHandle_t oledMutex = nullptr;

TaskHandle_t sensorTaskHandle = nullptr;
TaskHandle_t buttonTaskHandle = nullptr;

// ==================================================
// Application data and configuration
// ==================================================
struct SensorData {
float temperature;
float humidity;
bool valid;
uint32_t sequence;
};

struct AppConfig {
char deviceName[32];
char wifiSSID[33];
char wifiPassword[65];
float temperatureThreshold;
uint32_t samplingIntervalMs;
};

AppConfig config;

volatile uint32_t lastButtonMicros = 0;

bool configurationMode = false;
bool restartRequested = false;

portMUX_TYPE configMux = portMUX_INITIALIZER_UNLOCKED;

// ==================================================
// Configuration storage
// ==================================================
void loadConfiguration() {
preferences.begin("room-monitor", true);

String name = preferences.getString(
"name", "Smart Room Monitor"
);

String ssid = preferences.getString("ssid", "");
String password = preferences.getString("pass", "");

config.temperatureThreshold =
preferences.getFloat("threshold", 30.0f);

config.samplingIntervalMs =
preferences.getUInt("interval", DEFAULT_INTERVAL_MS);

preferences.end();

name.toCharArray(config.deviceName,

