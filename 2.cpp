#include <Arduino.h>
#include <Wire.h>
#include <U8g2lib.h>
#include <DHT.h>
#include <math.h>

// =============================================
// Hardware configuration
// =============================================
#define DHTPIN 32
#define DHTTYPE DHT11

#define OLED_SDA 27
#define OLED_SCL 25

#define CONFIG_BTN 33

// =============================================
// Hardware objects
// =============================================
DHT dht(DHTPIN, DHTTYPE);

U8G2_SH1106_128X64_NONAME_F_HW_I2C oled(
U8G2_R0,
U8X8_PIN_NONE
);

// =============================================
// Sensor data shared through a FreeRTOS queue
// =============================================
struct SensorData {
float temperature;
float humidity;
bool valid;
uint32_t sequence;
};

QueueHandle_t sensorQueue = nullptr;

// =============================================
// Button event flag
// The actual interrupt implementation will be
// added in Stage 3.
// =============================================
volatile bool buttonEvent = false;

// =============================================
// Task 1: Sensor acquisition
// Priority: 3
// =============================================
void sensorTask(void *parameter) {
SensorData data = {
NAN,
NAN,
false,
0
};

TickType_t lastWakeTime = xTaskGetTickCount();

for (;;) {
float temperature = dht.readTemperature();
float humidity = dht.readHumidity();

data.temperature = temperature;
data.humidity = humidity;

data.valid =
!isnan(temperature) &&
!isnan(humidity);

data.sequence++;

// Keep only the latest sensor reading.
xQueueOverwrite(sensorQueue, &data);

// Periodic scheduling without delay().
vTaskDelayUntil(
&lastWakeTime,
pdMS_TO_TICKS(2000)
);
}
}

// =============================================
// Task 2: OLED display
// Priority: 2
// =============================================
void displayTask(void *parameter) {
SensorData data = {
NAN,
NAN,
false,
0
};

for (;;) {
// Read the latest available sensor data.
if (xQueuePeek(
sensorQueue,
&data,
pdMS_TO_TICKS(100)
) == pdTRUE) {

oled.clearBuffer();

// Header
oled.setFont(u8g2_font_6x12_tf);
oled.drawStr(15, 12, "SMART ROOM MONITOR");
oled.drawHLine(0, 17, 128);

if (!data.valid) {
oled.drawStr(5, 36, "Sensor read failed!");
oled.drawStr(5, 52, "Check DHT11 wiring");
} else {
char tempText[24];
char humidityText[24];

snprintf(
tempText,
sizeof(tempText),
"Temp: %.1f C",
data.temperature
);

snprintf(
humidityText,
sizeof(humidityText),
"Humidity: %.1f %%",
data.humidity
);

oled.setFont(u8g2_font_ncenB14_tr);
oled.drawStr(5, 38, tempText);

oled.setFont(u8g2_font_6x12_tf);
oled.drawStr(5, 57, humidityText);
}

oled.sendBuffer();
}

// Yield CPU time to other tasks.
vTaskDelay(pdMS_TO_TICKS(100));
}
}

// =============================================
// Task 3: Serial logging
// Priority: 1
// =============================================
void loggingTask(void *parameter) {
SensorData data = {
NAN,
NAN,
false,
0
};

uint32_t lastSequence = 0;

for (;;) {
if (xQueuePeek(
sensorQueue,
&data,
pdMS_TO_TICKS(100)
) == pdTRUE) {

// Print each new sample only once.
if (data.sequence != lastSequence) {
lastSequence = data.sequence;

if (data.valid) {
Serial.printf(
"Temperature: %.1f C | "
"Humidity: %.1f %% RH\n",
data.temperature,
data.humidity
);
} else {
Serial.println("DHT11 reading failed.");
}
}
}

vTaskDelay(pdMS_TO_TICKS(100));
}
}

// =============================================
// Setup
// =============================================
void setup() {
Serial.begin(115200);

pinMode(CONFIG_BTN, INPUT_PULLUP);

Wire.begin(OLED_SDA, OLED_SCL);

dht.begin();
oled.begin();

oled.clearBuffer();
oled.setFont(u8g2_font_6x12_tf);
oled.drawStr(12, 25, "SMART ROOM MONITOR");
oled.drawStr(30, 45, "Initializing...");
oled.sendBuffer();

Serial.println("Smart Room Monitor starting...");

// Create a queue with one SensorData item.
sensorQueue = xQueueCreate(
1,
sizeof(SensorData)
);

if (sensorQueue == nullptr) {
Serial.println("Failed to create sensor queue!");

while (true) {
vTaskDelay(pdMS_TO_TICKS(1000));
}
}

// Initialize queue with invalid data.
SensorData initialData = {
NAN,
NAN,
false,
0
};

xQueueOverwrite(sensorQueue, &initialData);

// Create sensor acquisition task.
BaseType_t sensorResult = xTaskCreate(
sensorTask,
"SensorTask",
4096,
nullptr,
3,
nullptr
);

// Create OLED display task.
BaseType_t displayResult = xTaskCreate(
displayTask,
"DisplayTask",
4096,
nullptr,
2,
nullptr
);

// Create Serial logging task.
BaseType_t loggingResult = xTaskCreate(
loggingTask,
"LoggingTask",
3072,
nullptr,
1,
nullptr
);

if (sensorResult != pdPASS ||
displayResult != pdPASS ||
loggingResult != pdPASS) {

Serial.println("Failed to create one or more tasks!");
} else {
Serial.println("All FreeRTOS tasks created.");
}
}

// =============================================
// Main loop
// Application work runs in FreeRTOS tasks.
// =============================================
void loop() {
vTaskDelay(pdMS_TO_TICKS(1000));
}