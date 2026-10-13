# 🌡️ Smart Room Monitor

![Platform](https://img.shields.io/badge/platform-ESP32-blue)
![Framework](https://img.shields.io/badge/framework-Arduino-00979D)
![RTOS](https://img.shields.io/badge/RTOS-FreeRTOS-green)
![Course](https://img.shields.io/badge/course-CSE4052-orange)

An ESP32-based room monitor that reads temperature and relative humidity from a **DHT11** sensor, shows the values on a **128×64 SH1106 OLED**, and logs them over Serial. The project was built incrementally, from a simple polling loop to a FreeRTOS, interrupt-driven design with a Wi-Fi configuration portal.

> **Status:** All four firmware stages are written. Hardware verification of each stage (especially the portal in Stage 4) is still to be recorded — see [Project Status](#-project-status).

---

## 📑 Table of Contents

- [Features](#-features)
- [Firmware Stages](#-firmware-stages)
- [Hardware](#-hardware)
- [Software Requirements](#-software-requirements)
- [Quick Start](#-quick-start)
- [Configuration Portal (Stage 4)](#-configuration-portal-stage-4)
- [Architecture](#-architecture)
- [Troubleshooting](#-troubleshooting)
- [Security Notes](#-security-notes)
- [Project Status](#-project-status)
- [Repository Layout](#-repository-layout)

---

## ✨ Features

- Temperature and humidity sensing with a DHT11
- Live display on an SH1106 128×64 I²C OLED
- Serial logging at 115200 baud, including sensor error reporting
- FreeRTOS tasks separating sensing, display, logging and button handling
- Hardware-timer-driven sampling (sensor reads happen in a task, never in the ISR)
- Debounced button interrupt on GPIO 33
- SoftAP configuration portal with HTTP page and WebSocket (port 81) updates
- Settings stored in non-volatile memory via `Preferences`

---

## 🧱 Firmware Stages

The four `.cpp` files are **alternative development snapshots, not modules**. Each has its own `setup()` / `loop()` and globals, so **choose one** — never compile them together.

| File | Stage | What it adds |
|------|-------|--------------|
| `1.cpp` | Basic | DHT11 read, OLED update, Serial output every 2 s |
| `2.cpp` | FreeRTOS | Separate sensor, display and logging tasks; one-item queue holds the latest reading |
| `3.cpp` | Interrupts & timer | Debounced GPIO button interrupt; hardware timer wakes the sensor task; mutex-protected OLED. The button task only logs the event |
| `4.cpp` | Config portal | Everything above, plus `Preferences` storage, SoftAP portal, HTTP page, WebSocket + JSON config updates |

**Recommended:** use `4.cpp` for the full-featured version and keep the others as history.

---

## 🔌 Hardware

| Component / signal | ESP32 pin |
|--------------------|-----------|
| DHT11 data | GPIO 32 |
| SH1106 OLED SDA | GPIO 27 |
| SH1106 OLED SCL | GPIO 25 |
| Config push button | GPIO 33 (`INPUT_PULLUP`, other side to GND) |
| Serial Monitor | 115200 baud |

```text
            ┌───────────┐
 DHT11 ─────┤ GPIO 32   │
 OLED SDA ──┤ GPIO 27   │
 OLED SCL ──┤ GPIO 25   │
 Button ────┤ GPIO 33   │   (button → GND when pressed)
            │   ESP32   │
            └───────────┘
```

The display is assumed to be a **128×64 SH1106 over I²C**. Power the DHT11 and OLED according to their datasheets (typically 3.3 V).

---

## 📦 Software Requirements

Install via Arduino IDE **Library Manager**, depending on the stage you choose:

| Library | Stages |
|---------|--------|
| DHT sensor library (Adafruit) | 1–4 |
| Adafruit Unified Sensor | 1–4 |
| U8g2 | 1–4 |
| ArduinoJson | 4 |
| WebSockets (provides `WebSocketsServer`) | 4 |

`Wire`, `WiFi`, `WebServer`, `Preferences` and FreeRTOS come with the ESP32 Arduino core.

> ⚠️ `3.cpp` and `4.cpp` use the **ESP32 Arduino core 3.x** hardware-timer API. Select a compatible board package version or you will get compile errors around the timer functions.

---

## 🚀 Quick Start

1. Install the ESP32 board package (core 3.x for Stages 3–4) and the libraries above.
2. Create a sketch folder named `SmartRoomMonitor`.
3. Copy your chosen stage (e.g. `4.cpp`) into `SmartRoomMonitor/SmartRoomMonitor.ino`. The `.ino` name must match the folder name.
4. Select your ESP32 board and port, then upload.
5. Open Serial Monitor at **115200 baud**.
6. Confirm the OLED initializes and valid temperature/humidity values appear.

---

## 📶 Configuration Portal (Stage 4)

The portal starts when **GPIO 33 is held LOW during boot or reset**.

1. Hold the button on GPIO 33.
2. Reset or power-cycle the ESP32 while still holding it.
3. Connect to the Wi-Fi network **`SmartRoomMonitor-Setup`**.
4. Enter the hard-coded password from the source (default: `RoomSetup123`).
5. Open the access-point IP address printed in Serial Monitor (usually the standard ESP32 SoftAP address).
6. Edit settings on the page. It talks to the ESP32 over WebSocket on port 81, and values are validated and saved to flash.

### Settings

| Setting | Default | Accepted range |
|---------|---------|----------------|
| Device name | `Smart Room Monitor` | — |
| Temperature threshold | 30.0 °C | −20 to 80 °C |
| Sampling interval | 2000 ms | 2000–60000 ms, 1000 ms steps |
| Normal-mode Wi-Fi SSID / password | — | Stored only (see limitations) |
| SoftAP SSID | `SmartRoomMonitor-Setup` | Set in source |
| SoftAP password | `RoomSetup123` | Set in source — **change it** |

### Known limitations

- **Normal-mode Wi-Fi is not implemented yet.** Saved SSID/password are stored but the device does not currently connect to that network.
- **No captive portal.** Use the IP address printed in Serial Monitor.
- **No automatic restart after saving.** `restartRequested` is checked by the portal loop but isn't set by the save handler; reset the board manually.
- The button task in Stage 3 only logs events; it does not open the portal. Portal entry is decided at boot.

---

## 🏗️ Architecture

| Component | Responsibility |
|-----------|----------------|
| **Sensor task** | Reads DHT11 temperature and humidity |
| **Display task** | Renders the latest reading on the OLED |
| **Logging task** | Prints new readings or sensor errors to Serial |
| **Button task** | Handles notifications from the GPIO interrupt |
| **Timer ISR** | Notifies the sensor task when a sample is due (no sensor I/O in the ISR) |
| **Latest-reading queue** | One-item queue shared between tasks |
| **OLED mutex** | Protects display access (Stages 3–4) |
| **Portal servers** | HTTP + WebSocket (port 81) servers, active only in configuration mode (Stage 4) |

---

## 🛠️ Troubleshooting

| Symptom | What to check |
|---------|---------------|
| "DHT11 reading failed" / no values | GPIO 32 wiring, power, ground, and that the sensor is really a DHT11 |
| Blank OLED | Display is SH1106 128×64 I²C; SDA on GPIO 27, SCL on GPIO 25 |
| Button not detected | Button connects GPIO 33 to GND; chosen stage includes interrupt handling (Stage 3+) |
| `SmartRoomMonitor-Setup` not visible | Running Stage 4 as a sketch; button held during reset; check Serial for SoftAP messages |
| Timer compile errors | ESP32 Arduino core version must be 3.x for `3.cpp` / `4.cpp` |
| Missing header (`WebSocketsServer.h`) | Install the WebSockets library listed above |

---

## 🔒 Security Notes

The SoftAP credentials are hard-coded in the source and published in this README, so treat them as **not secure**. Change them before any real deployment and avoid committing real home Wi-Fi credentials.

---

## ✅ Project Status

| Stage | Written | Verified on hardware |
|-------|:-------:|:--------------------:|
| 1 – Basic | ✅ | ✅ |
| 2 – FreeRTOS | ✅ | ✅ |
| 3 – Interrupts & timer | ✅ | ✅ |
| 4 – Config portal | ✅ | ✅ |

Tick the boxes, and add screenshots and known issues, once each stage has been tested on the physical device. The source alone does not prove that the access point, web page, WebSocket link and saving all work.

**Possible next steps:** join the saved Wi-Fi network in normal mode, restart after saving settings, open the portal from the button task, add a captive portal.

---

## 📁 Repository Layout

```text
Smart-Room-Monitor/
├── README.md
├── firmware/
│   └── SmartRoomMonitor.ino
├── development/
│   ├── 1.cpp
│   ├── 2.cpp
│   ├── 3.cpp
│   └── 4.cpp

```

---