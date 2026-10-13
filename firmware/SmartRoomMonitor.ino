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
sizeof(config.deviceName));

ssid.toCharArray(config.wifiSSID,
sizeof(config.wifiSSID));

password.toCharArray(config.wifiPassword,
sizeof(config.wifiPassword));

if (config.samplingIntervalMs < MIN_INTERVAL_MS ||
config.samplingIntervalMs > MAX_INTERVAL_MS) {
config.samplingIntervalMs = DEFAULT_INTERVAL_MS;
}

if (!isfinite(config.temperatureThreshold) ||
config.temperatureThreshold < -20.0f ||
config.temperatureThreshold > 80.0f) {
config.temperatureThreshold = 30.0f;
}
}

void saveConfiguration() {
preferences.begin("room-monitor", false);

preferences.putString("name", config.deviceName);
preferences.putString("ssid", config.wifiSSID);
preferences.putString("pass", config.wifiPassword);
preferences.putFloat("threshold",
config.temperatureThreshold);
preferences.putUInt("interval",
config.samplingIntervalMs);

preferences.end();
}

// ==================================================
// Configuration HTML page
// ==================================================
const char CONFIG_HTML[] PROGMEM = R"HTML(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Smart Room Monitor</title>
<style>
body {
font-family: Arial, sans-serif;
max-width: 560px;
margin: 25px auto;
padding: 16px;
background: #f2f5f9;
color: #202938;
}
.card {
background: white;
padding: 22px;
border-radius: 14px;
box-shadow: 0 3px 12px #0001;
}
label { display:block; margin-top:15px; font-weight:bold; }
input {
width:100%; box-sizing:border-box;
padding:11px; margin-top:6px;
border:1px solid #ccd3dd; border-radius:7px;
}
button {
width:100%; padding:12px; margin-top:18px;
border:0; border-radius:7px;
background:#315efb; color:white; font-size:16px;
}
#status { margin-top:12px; font-size:14px; }
</style>
</head>
<body>
<div class="card">
<h2>Smart Room Monitor</h2>
<p>Device configuration portal</p>
<p>WebSocket: <strong id="connection">Connecting...</strong></p>

<form id="configForm">
<label>Device name</label>
<input id="name" maxlength="31" required>

<label>Temperature threshold (°C)</label>
<input id="threshold" type="number"
min="-20" max="80" step="0.1" required>

<label>Sampling interval (milliseconds)</label>
<input id="interval" type="number"
min="2000" max="60000" step="1000" required>

<label>Normal-mode Wi-Fi SSID</label>
<input id="ssid" maxlength="32" autocomplete="off">

<label>Normal-mode Wi-Fi password</label>
<input id="password" type="password"
maxlength="63" autocomplete="new-password">

<button type="submit">Save configuration</button>
</form>

<p id="status">Connecting to device...</p>
</div>

<script>
let socket;
let configLoaded = false;

function connectSocket() {
socket = new WebSocket('ws://' + location.hostname + ':81/');

socket.onopen = () => {
document.getElementById('connection').textContent =
'Connected';
socket.send(JSON.stringify({action:'get'}));
};

socket.onclose = () => {
document.getElementById('connection').textContent =
'Disconnected';
setTimeout(connectSocket, 2000);
};

socket.onerror = () => socket.close();

socket.onmessage = (event) => {
const data = JSON.parse(event.data);

if (data.action === 'config') {
document.getElementById('name').value = data.name;
document.getElementById('threshold').value =
data.threshold;
document.getElementById('interval').value =
data.interval;
document.getElementById('ssid').value = data.ssid || '';
document.getElementById('password').value = '';
configLoaded = true;
document.getElementById('status').textContent =
'Configuration loaded.';
}

if (data.action === 'saved') {
document.getElementById('status').textContent =
'Settings saved. Restart if requested.';
}

if (data.action === 'error') {
document.getElementById('status').textContent =
data.message;
}
};
}

document.getElementById('configForm').addEventListener(
'submit', (event) => {
event.preventDefault();

if (!socket || socket.readyState !== WebSocket.OPEN) {
document.getElementById('status').textContent =
'WebSocket is not connected.';
return;
}

if (!configLoaded) {
document.getElementById('status').textContent =
'Wait for configuration to load.';
return;
}

const payload = {
action: 'save',
name: document.getElementById('name').value.trim(),
threshold: Number(
document.getElementById('threshold').value
),
interval: Number(
document.getElementById('interval').value
),
ssid: document.getElementById('ssid').value.trim(),
password: document.getElementById('password').value
};

socket.send(JSON.stringify(payload));
}
);

connectSocket();
</script>
</body>
</html>
)HTML";

// ==================================================
// WebSocket helpers
// ==================================================
void sendConfiguration(uint8_t client) {
JsonDocument doc;

doc["action"] = "config";
doc["name"] = config.deviceName;
doc["threshold"] = config.temperatureThreshold;
doc["interval"] = config.samplingIntervalMs;
doc["ssid"] = config.wifiSSID;

// Never send the stored Wi-Fi password to the browser.
String output;
serializeJson(doc, output);
webSocket.sendTXT(client, output);
}

void sendWebSocketError(uint8_t client, const char *message) {
JsonDocument doc;
doc["action"] = "error";
doc["message"] = message;

String output;
serializeJson(doc, output);
webSocket.sendTXT(client, output);
}

void webSocketEvent(
uint8_t client,
WStype_t type,
uint8_t *payload,
size_t length
) {
if (type == WStype_CONNECTED) {
Serial.println("Configuration browser connected.");
sendConfiguration(client);
return;
}

if (type != WStype_TEXT) {
return;
}

JsonDocument doc;

DeserializationError error =
deserializeJson(doc, payload, length);

if (error) {
sendWebSocketError(client, "Invalid JSON.");
return;
}

const char *action = doc["action"] | "";

if (strcmp(action, "get") == 0) {
sendConfiguration(client);
return;
}

if (strcmp(action, "save") != 0) {
sendWebSocketError(client, "Unknown action.");
return;
}

const char *name = doc["name"] | "";
const char *ssid = doc["ssid"] | "";
const char *password = doc["password"] | "";

float threshold = doc["threshold"] | NAN;
uint32_t interval = doc["interval"] | 0;

if (strlen(name) == 0 ||
strlen(name) >= sizeof(config.deviceName)) {
sendWebSocketError(client, "Invalid device name.");
return;
}

if (strlen(ssid) > 32 || strlen(password) > 63) {
sendWebSocketError(client, "Invalid Wi-Fi credentials.");
return;
}

if (!isfinite(threshold) ||
threshold < -20.0f || threshold > 80.0f) {
sendWebSocketError(client, "Threshold must be -20 to 80 C.");
return;
}

if (interval < MIN_INTERVAL_MS ||
interval > MAX_INTERVAL_MS ||
interval % 1000 != 0) {
sendWebSocketError(
client, "Interval must be 2000-60000 ms in 1000 ms steps."
);
return;
}

// Copy validated settings into bounded buffers.
portENTER_CRITICAL(&configMux);

strlcpy(config.deviceName, name, sizeof(config.deviceName));
strlcpy(config.wifiSSID, ssid, sizeof(config.wifiSSID));
strlcpy(config.wifiPassword, password,
sizeof(config.wifiPassword));

config.temperatureThreshold = threshold;
config.samplingIntervalMs = interval;

portEXIT_CRITICAL(&configMux);

saveConfiguration();

// This configuration portal stores normal-mode Wi-Fi
// credentials, but joining that network is a later feature.
JsonDocument response;
response["action"] = "saved";

String output;
serializeJson(response, output);
webSocket.sendTXT(client, output);

Serial.println("Configuration saved to NVS.");
}

// ==================================================
// Configuration portal
// ==================================================
void runConfigurationPortal() {
configurationMode = true;

Serial.println();
Serial.println("=== CONFIGURATION MODE ===");

WiFi.mode(WIFI_AP);

if (!WiFi.softAP("SmartRoomMonitor-Setup",
"RoomSetup123")) {
Serial.println("Failed to start SoftAP.");

while (true) {
delay(1000);
}
}

Serial.print("Connect to Wi-Fi: ");
Serial.println("SmartRoomMonitor-Setup");

Serial.print("Password: ");
Serial.println("RoomSetup123");

Serial.print("Open browser at: http://");
Serial.println(WiFi.softAPIP());

webServer.on("/", HTTP_GET, []() {
webServer.send_P(
200,
"text/html",
CONFIG_HTML
);
});

webServer.onNotFound([]() {
webServer.send(404, "text/plain", "Not found");
});

webServer.begin();

webSocket.begin();
webSocket.onEvent(webSocketEvent);

for (;;) {
webServer.handleClient();
webSocket.loop();

if (restartRequested) {
delay(200);
ESP.restart();
}

delay(2);
}
}

// ==================================================
// GPIO interrupt: button
// ==================================================
void IRAM_ATTR onButtonPressed() {
uint32_t now = micros();

if ((uint32_t)(now - lastButtonMicros) >= DEBOUNCE_US) {
lastButtonMicros = now;

BaseType_t higherPriorityTaskWoken = pdFALSE;

vTaskNotifyGiveFromISR(
buttonTaskHandle,
&higherPriorityTaskWoken
);

if (higherPriorityTaskWoken == pdTRUE) {
portYIELD_FROM_ISR();
}
}
}

// ==================================================
// Hardware timer interrupt
// ==================================================
void ARDUINO_ISR_ATTR onSamplingTimer() {
BaseType_t higherPriorityTaskWoken = pdFALSE;

vTaskNotifyGiveFromISR(
sensorTaskHandle,
&higherPriorityTaskWoken
);

if (higherPriorityTaskWoken == pdTRUE) {
portYIELD_FROM_ISR();
}
}

// ==================================================
// FreeRTOS sensor task
// ==================================================
void sensorTask(void *parameter) {
SensorData data = {NAN, NAN, false, 0};

for (;;) {
ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

data.temperature = dht.readTemperature();
data.humidity = dht.readHumidity();

data.valid = !isnan(data.temperature) &&
!isnan(data.humidity);

data.sequence++;

xQueueOverwrite(sensorQueue, &data);
}
}

// ==================================================
// FreeRTOS OLED task
// ==================================================
void displayTask(void *parameter) {
SensorData data = {NAN, NAN, false, 0};

for (;;) {
if (xQueuePeek(sensorQueue, &data,
pdMS_TO_TICKS(100)) == pdTRUE) {

if (xSemaphoreTake(oledMutex,
pdMS_TO_TICKS(100)) == pdTRUE) {

oled.clearBuffer();
oled.setFont(u8g2_font_6x12_tf);
oled.drawStr(0, 12, config.deviceName);
oled.drawHLine(0, 17, 128);

if (!data.valid) {
oled.drawStr(5, 36, "Sensor read failed!");
} else {
char tempText[24];
char humidityText[24];

snprintf(tempText, sizeof(tempText),
"Temp: %.1f C", data.temperature);

snprintf(humidityText, sizeof(humidityText),
"Humidity: %.1f %%", data.humidity);

oled.setFont(u8g2_font_ncenB14_tr);
oled.drawStr(5, 38, tempText);

oled.setFont(u8g2_font_6x12_tf);
oled.drawStr(5, 57, humidityText);

if (data.temperature >= config.temperatureThreshold) {
oled.drawBox(119, 0, 8, 8);
}
}

oled.sendBuffer();
xSemaphoreGive(oledMutex);
}
}

vTaskDelay(pdMS_TO_TICKS(100));
}
}

// ==================================================
// FreeRTOS logging task
// ==================================================
void loggingTask(void *parameter) {
SensorData data = {NAN, NAN, false, 0};
uint32_t lastSequence = 0;

for (;;) {
if (xQueuePeek(sensorQueue, &data,
pdMS_TO_TICKS(100)) == pdTRUE &&
data.sequence != lastSequence) {

lastSequence = data.sequence;

if (data.valid) {
Serial.printf(
"Temperature: %.1f C | Humidity: %.1f %% RH\n",
data.temperature, data.humidity
);
} else {
Serial.println("DHT11 reading failed.");
}
}

vTaskDelay(pdMS_TO_TICKS(100));
}
}

// ==================================================
// Button task
// ==================================================
void buttonTask(void *parameter) {
for (;;) {
ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

Serial.println("Button pressed: event received.");
// Future configuration actions can be added here.
}
}

// ==================================================
// Setup
// ==================================================
void setup() {
Serial.begin(115200);

// Check button before starting normal operation.
pinMode(CONFIG_BTN, INPUT_PULLUP);
delay(30);

bool enterConfiguration =
digitalRead(CONFIG_BTN) == LOW;

loadConfiguration();

if (enterConfiguration) {
runConfigurationPortal();
return;
}

Wire.begin(OLED_SDA, OLED_SCL);
dht.begin();
oled.begin();

oled.clearBuffer();
oled.setFont(u8g2_font_6x12_tf);
oled.drawStr(12, 25, "SMART ROOM MONITOR");
oled.drawStr(30, 45, "Initializing...");
oled.sendBuffer();

sensorQueue = xQueueCreate(1, sizeof(SensorData));
oledMutex = xSemaphoreCreateMutex();

if (sensorQueue == nullptr || oledMutex == nullptr) {
Serial.println("Failed to create FreeRTOS resources.");
while (true) {
delay(1000);
}
}

SensorData initialData = {NAN, NAN, false, 0};
xQueueOverwrite(sensorQueue, &initialData);

if (xTaskCreate(sensorTask, "SensorTask", 4096,
nullptr, 3, &sensorTaskHandle) != pdPASS ||
xTaskCreate(displayTask, "DisplayTask", 4096,
nullptr, 2, nullptr) != pdPASS ||
xTaskCreate(loggingTask, "LoggingTask", 3072,
nullptr, 1, nullptr) != pdPASS ||
xTaskCreate(buttonTask, "ButtonTask", 3072,
nullptr, 2, &buttonTaskHandle) != pdPASS) {

Serial.println("Failed to create FreeRTOS tasks.");
while (true) {
delay(1000);
}
}

attachInterrupt(
digitalPinToInterrupt(CONFIG_BTN),
onButtonPressed,
FALLING
);

samplingTimer = timerBegin(1000000);

if (samplingTimer == nullptr) {
Serial.println("Timer initialization failed.");
while (true) {
delay(1000);
}
}

timerAttachInterrupt(samplingTimer, &onSamplingTimer);

timerAlarm(
samplingTimer,
config.samplingIntervalMs * 1000ULL,
true,
0
);

Serial.println("Smart Room Monitor started.");
Serial.println("Hold GPIO 33 button while resetting");
Serial.println("to enter configuration mode.");
}

void loop() {
vTaskDelay(pdMS_TO_TICKS(1000));
}