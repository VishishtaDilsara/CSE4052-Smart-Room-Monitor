
    
  
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

