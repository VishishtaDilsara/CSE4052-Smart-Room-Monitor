
    
  
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

