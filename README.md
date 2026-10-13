# Smart Room Monitor

An ESP32-based Smart Room Monitor project for the CSE4052 course. The
firmware reads temperature and relative humidity from a DHT11 sensor,
displays the readings on a 128×64 SH1106 OLED, and reports sensor data
through the Serial Monitor. Later development stages introduce FreeRTOS
tasks, interrupt-driven sampling, and a Wi-Fi configuration portal.

## Features in the uploaded code

The four uploaded `.cpp` files represent **development stages /
alternative firmware versions**. They are not four modules that should
be compiled together.

  -----------------------------------------------------------------------
  Uploaded file                       What it contains
  ----------------------------------- -----------------------------------
  `1.cpp`                             Basic implementation: reads the
                                      DHT11, updates the OLED, and prints
                                      readings to Serial every two
                                      seconds.

  `2.cpp`                             FreeRTOS version: separates sensor
                                      acquisition, OLED display, and
                                      Serial logging into tasks. A
                                      one-item queue keeps the latest
                                      sensor reading available to the
                                      tasks.

  `3.cpp`                             Interrupt/timer version: adds a
                                      GPIO button interrupt with debounce
                                      and a hardware timer that wakes the
                                      sensor task at the configured
                                      interval. The button task currently
                                      logs the event; it does not open
                                      the configuration portal.

  `4.cpp`                             Most feature-rich version: adds
                                      persistent settings using
                                      `Preferences`, a SoftAP
                                      configuration portal, an HTTP page,
                                      WebSocket messages, JSON-based
                                      configuration updates, FreeRTOS
                                      tasks, and timer-driven sensor
                                      sampling.
  -----------------------------------------------------------------------

**Recommended starting point:** use `4.cpp` if you want to work on the
configuration-portal version. Keep the earlier files as development
snapshots. Before uploading, rename the selected file to
`SmartRoomMonitor.ino` and place it in a matching Arduino sketch folder
named `SmartRoomMonitor`.

## Hardware

  Component / signal          ESP32 connection
  --------------------------- ---------------------------------------
  DHT11 data                  GPIO 32
  SH1106 OLED SDA             GPIO 27
  SH1106 OLED SCL             GPIO 25
  Configuration push button   GPIO 33, configured as `INPUT_PULLUP`
  Serial Monitor              115200 baud

The OLED code assumes a 128×64 SH1106 I²C display. With `INPUT_PULLUP`,
the button is expected to pull GPIO 33 to GND when pressed.

## Required Arduino libraries

Install the following libraries through Arduino IDE Library Manager, as
applicable to the selected firmware version:

-   **DHT sensor library** (Adafruit)
-   **Adafruit Unified Sensor**
-   **U8g2**
-   **ArduinoJson** (used by `4.cpp`)
-   **WebSockets** library providing `WebSocketsServer` (used by
    `4.cpp`)

`Wire`, `WiFi`, `WebServer`, `Preferences`, and FreeRTOS facilities are
provided by the ESP32 Arduino core. The hardware-timer calls in `3.cpp`
and `4.cpp` use the ESP32 Arduino core 3.x API, so select a compatible
ESP32 board package version.

## Build and upload

1.  Open Arduino IDE and install/select an ESP32 board supported by your
    installed ESP32 Arduino core.
2.  Choose **one** firmware stage. Do not combine `1.cpp`, `2.cpp`,
    `3.cpp`, and `4.cpp`, because each contains its own global objects
    and `setup()` / `loop()` functions.
3.  Copy the chosen code into `SmartRoomMonitor/SmartRoomMonitor.ino`
    (the `.ino` filename should match the sketch folder name).
4.  Install the libraries required for that stage.
5.  Connect the ESP32, select the correct board and port, and upload.
6.  Open Serial Monitor at **115200 baud**.
7.  Check that the OLED initializes and that valid temperature and
    humidity readings appear. If readings fail, check the DHT11 wiring
    and sensor type.

## Using the configuration portal in `4.cpp`

The portal code is entered when GPIO 33 is held LOW while the ESP32
starts or resets.

1.  Hold the button connected to GPIO 33.
2.  Reset or power-cycle the ESP32 while keeping the button pressed.
3.  Look in the device's Wi-Fi list for **`SmartRoomMonitor-Setup`**.
4.  The access-point password currently hard-coded in the source is
    **`RoomSetup123`**.
5.  Connect to that network and open the access-point IP printed in
    Serial Monitor (commonly the ESP32 SoftAP address).
6.  Use the configuration page to edit the device name, temperature
    threshold, sampling interval, and normal-mode Wi-Fi credentials.

The page communicates with the ESP32 using WebSockets on port 81.
Configuration values are validated and saved to ESP32 non-volatile
storage using `Preferences`.

### Important portal limitations

-   The source explicitly says that joining the configured normal-mode
    Wi-Fi network is a later feature. Saving an SSID/password does
    **not** mean the device currently connects to that network.
-   The current portal code does not implement a captive portal; use the
    IP address printed in Serial Monitor.
-   The `restartRequested` flag is checked by the portal loop but is not
    set by the shown configuration-save handler. Do not assume that
    saving settings automatically restarts the device.
-   The access-point credentials are hard-coded in the source. Change
    them before any real deployment and do not treat the current
    password as secure.
-   Verify portal operation on the actual hardware. The source code
    alone does not prove that the access point, browser page, WebSocket
    connection, and saving process have all been tested successfully.

## Configuration options in `4.cpp`

  -----------------------------------------------------------------------
  Setting                             Default / allowed range in code
  ----------------------------------- -----------------------------------
  Device name                         Default: `Smart Room Monitor`

  Temperature threshold               Default: 30.0 °C; accepted range:
                                      −20 to 80 °C

  Sampling interval                   Default: 2000 ms; accepted range:
                                      2000--60000 ms in 1000 ms steps

  Configuration SoftAP SSID           `SmartRoomMonitor-Setup`

  Configuration SoftAP password       `RoomSetup123` (hard-coded; change
                                      before deployment)
  -----------------------------------------------------------------------

## Firmware architecture

In the later stages, FreeRTOS tasks separate the main responsibilities:

-   **Sensor task:** reads temperature and humidity from the DHT11.
-   **Display task:** renders the latest sensor reading on the OLED.
-   **Logging task:** prints new readings or sensor errors to Serial.
-   **Button task:** processes button notifications from the GPIO
    interrupt.
-   **Hardware timer ISR:** notifies the sensor task when a new sample
    is due; sensor reads are performed in the task, not in the ISR.

The later versions use a one-item queue to retain the latest sensor
sample. In `3.cpp` and `4.cpp`, a mutex protects OLED access. The
configuration portal in `4.cpp` runs an HTTP server and WebSocket server
while in configuration mode.

## Troubleshooting

-   **No sensor values / "DHT11 reading failed":** check GPIO 32, power,
    ground, and the sensor model.
-   **OLED is blank:** verify the display is SH1106 128×64 I²C and check
    SDA GPIO 27 and SCL GPIO 25.
-   **Button event is not detected:** verify the button connects GPIO 33
    to GND when pressed and that the selected stage includes
    button-interrupt handling.
-   **`SmartRoomMonitor-Setup` does not appear:** make sure you are
    running `4.cpp` converted to the Arduino sketch, hold the button
    while resetting, and check Serial Monitor for the SoftAP startup
    messages.
-   **Compilation errors around timer functions:** check that the
    installed ESP32 Arduino core matches the timer API used in the
    selected source. `3.cpp` and `4.cpp` are written for the ESP32
    Arduino core 3.x timer API.
-   **Missing header errors:** install the libraries listed above and
    confirm that the library providing `WebSocketsServer.h` is installed
    for `4.cpp`.

## Repository layout suggestion

``` text
Smart-Room-Monitor/
├── README.md
├── firmware/
│   └── SmartRoomMonitor/
│       └── SmartRoomMonitor.ino
├── development/
│   ├── 1.cpp
│   ├── 2.cpp
│   ├── 3.cpp
│   └── 4.cpp
├── docs/
└── screenshots/
```

You can keep the four uploaded files under `development/` as historical
versions and place the chosen, renamed Arduino sketch under
`firmware/SmartRoomMonitor/`. If you prefer to store only the final
implementation, keep `4.cpp` as the source of the final sketch and
document the earlier versions in your project history.

## Current implementation status

The source code includes sensor monitoring and display, FreeRTOS-based
processing, interrupt-driven sampling, and a configuration portal
implementation in the final stage. Test each function on the physical
hardware and update this README with verified results, screenshots, and
any known issues before submitting the project.

