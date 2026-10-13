#include <Arduino.h>
#include <Wire.h>
#include <U8g2lib.h>
#include <DHT.h>
#include <math.h>

// ==================================================
// Hardware configuration
// ==================================================
#define DHTPIN 32
#define DHTTYPE DHT11

#define OLED_SDA 27
#define OLED_SCL 25

#define CONFIG_BTN 33

#define SAMPLE_INTERVAL_MS 2000
#define BUTTON_DEBOUNCE_US 200000

// ==================================================
// Hardware objects
// ==================================================
DHT dht(DHTPIN, DHTTYPE);

U8G2_SH1106_128X64_NONAME_F_HW_I2C oled(
U8G2_R0,
U8X8_PIN_NONE
);

// ==================================================
// Sensor data
// ==================================================
struct SensorData {
float temperature;
float humidity;
bool valid;
uint32_t sequence;
};

QueueHandle_t sensorQueue = nullptr;
SemaphoreHandle_t oledMutex = nullptr;

TaskHandle_t sensorTaskHandle = nullptr;
TaskHandle_t buttonTaskHandle = nullptr;

hw_timer_t *samplingTimer = nullptr;

// Used only by the button ISR for debouncing.
volatile uint32_t lastButtonMicros = 0;

// ==================================================
// ISR 1: GPIO push-button interrupt
//
// Keep the ISR short. Notify a task and return.
// ==================================================
void IRAM_ATTR onButtonPressed() {
uint32_t now = micros();

if ((uint32_t)(now - lastButtonMicros) >=
BUTTON_DEBOUNCE_US) {

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
// ISR 2: Hardware timer interrupt
//
// The timer wakes the sensor task.
// Sensor reading is NOT performed in the ISR.
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
// Task 1: Sensor acquisition
// Priority: 3
// Wakes when notified by the hardware timer.
// ==================================================
void sensorTask(void *parameter) {
SensorData data = {
NAN,
NAN,
false,
0
};

for (;;) {
// Sleep until the timer ISR signals a sample.
ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

float temperature = dht.readTemperature();
float humidity = dht.readHumidity();

data.temperature = temperature;
data.humidity = humidity;

data.valid =
!isnan(temperature) &&
!isnan(humidity);

data.sequence++;

// Queue length is one: retain latest reading.
xQueueOverwrite(sensorQueue, &data);
}
}

// ==================================================
// Task 2: OLED display
// Priority: 2
// ==================================================
void displayTask(void *parameter) {
SensorData data = {
NAN,
NAN,
false,
0
};

for (;;) {
if (xQueuePeek(
sensorQueue,
&data,
pdMS_TO_TICKS(100)
) == pdTRUE) {

if (xSemaphoreTake(
oledMutex,
pdMS_TO_TICKS(100)
) == pdTRUE) {

oled.clearBuffer();

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

xSemaphoreGive(oledMutex);
}
}

vTaskDelay(pdMS_TO_TICKS(100));
}
}

// ==================================================
// Task 3: Serial logging
// Priority: 1
// ==================================================
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

if (data.sequence != lastSequence) {
lastSequence = data.sequence;

if (data.valid) {
Serial.printf(
"Sample %lu | Temperature: %.1f C | "
"Humidity: %.1f %% RH\n",
(unsigned long)data.sequence,
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

// ==================================================
// Task 4: Button event processing
// Priority: 2
//
// The task sleeps until the GPIO ISR notifies it.
// ==================================================
void buttonTask(void *parameter) {
for (;;) {
ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

Serial.println(
"[INTERRUPT] Configuration button pressed."
);

// The configuration portal will be integrated
// into this event flow in a later stage.
}
}

// ==================================================
// Setup
// ==================================================
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

// Create the latest-reading queue.
sensorQueue = xQueueCreate(
1,
sizeof(SensorData)
);

// Protect shared OLED access if more tasks
// are added to use the display later.
oledMutex = xSemaphoreCreateMutex();

if (sensorQueue == nullptr ||
oledMutex == nullptr) {

Serial.println("Failed to create RTOS resources!");

while (true) {
vTaskDelay(pdMS_TO_TICKS(1000));
}
}

SensorData initialData = {
NAN,
NAN,
false,
0
};

xQueueOverwrite(sensorQueue, &initialData);

// Create the sensor task first so its handle
// is available before starting the timer.
if (xTaskCreate(
sensorTask,
"SensorTask",
4096,
nullptr,
3,
&sensorTaskHandle
) != pdPASS) {

Serial.println("Sensor task creation failed!");

while (true) {
vTaskDelay(pdMS_TO_TICKS(1000));
}
}

if (xTaskCreate(
displayTask,
"DisplayTask",
4096,
nullptr,
2,
nullptr
) != pdPASS) {

Serial.println("Display task creation failed!");

while (true) {
vTaskDelay(pdMS_TO_TICKS(1000));
}
}

if (xTaskCreate(
loggingTask,
"LoggingTask",
3072,
nullptr,
1,
nullptr
) != pdPASS) {

Serial.println("Logging task creation failed!");

while (true) {
vTaskDelay(pdMS_TO_TICKS(1000));
}
}

if (xTaskCreate(
buttonTask,
"ButtonTask",
3072,
nullptr,
2,
&buttonTaskHandle
) != pdPASS) {

Serial.println("Button task creation failed!");

while (true) {
vTaskDelay(pdMS_TO_TICKS(1000));
}
}

// Configure the GPIO interrupt only after the
// button task has been created.
attachInterrupt(
digitalPinToInterrupt(CONFIG_BTN),
onButtonPressed,
FALLING
);

// ESP32 Arduino core 3.x timer API.
// Timer counter frequency = 1 MHz.
samplingTimer = timerBegin(1000000);

if (samplingTimer == nullptr) {
Serial.println("Timer initialization failed!");

while (true) {
vTaskDelay(pdMS_TO_TICKS(1000));
}
}

timerAttachInterrupt(
samplingTimer,
&onSamplingTimer
);

// Generate a timer event every 2 seconds.
timerAlarm(
samplingTimer,
SAMPLE_INTERVAL_MS * 1000ULL,
true,
0
);

Serial.println("All FreeRTOS tasks created.");
Serial.println("GPIO interrupt enabled on GPIO 33.");
Serial.println("Hardware timer sampling enabled.");
}

// ==================================================
// Main loop
// ==================================================
void loop() {
vTaskDelay(pdMS_TO_TICKS(1000));
}