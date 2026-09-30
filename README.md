# ⚡ JK-BMS Smart Monitor & BLE-to-Wi-Fi Adapter (ESP32-S3 & ESP32-C6)

[![PlatformIO](https://img.shields.io/badge/PlatformIO-Build%20Passed-orange?logo=platformio)](https://platformio.org/)
[![Hardware](https://img.shields.io/badge/Hardware-ESP32--S3%20%7C%20ESP32--C6%20Super%20Mini-blue?logo=espressif)](https://www.espressif.com/)
[![Protocol](https://img.shields.io/badge/BMS%20Protocol-JK02%20BLE-green)](https://github.com/tigersumy/JK-BMS-Smart-Monitor-ESP32-s3)
[![Framework](https://img.shields.io/badge/Framework-Arduino%20ESP32-red?logo=arduino)](https://github.com/espressif/arduino-esp32)
[![Pack Support](https://img.shields.io/badge/Battery%20Packs-4S%20%7C%208S%20%7C%2016S-brightgreen)](#-multi-cell-pack-support-4s--8s--16s)
[![UI Language](https://img.shields.io/badge/Language-EN%20%7C%20UA-purple)](#-bilingual-user-interface)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)

> **Autonomous wireless IoT bridge and diagnostic web monitor for JK-BMS (JiKong BMS) battery management systems using ultra-compact ESP32-S3 and ESP32-C6 (Wi-Fi 6 & RISC-V) Super Mini.**  
> *Zero hardcoded credentials, Captive Portal initial provisioning, BLE continuous stream telemetry, dynamic responsive dark-themed dashboard, multi-cell (4S/8S/16S) support, and complete switch control (Charge MOS, Discharge MOS, Active Balancer).*

---

## 📑 Table of Contents

- [Key Features](#-key-features)
- [System Architecture](#-system-architecture)
- [Hardware & Pinout](#-hardware--pinout)
- [Bilingual Interface (EN / UA)](#-bilingual-user-interface)
- [Multi-Cell Pack Support (4S / 8S / 16S)](#-multi-cell-pack-support-4s--8s--16s)
- [Tailscale VPN Remote Access (ESP32-S3)](#-tailscale-vpn-remote-access-esp32-s3)
- [REST API Specification](#-rest-api-specification)
- [Getting Started](#-getting-started)
  - [Pre-compiled Firmware Binaries](#pre-compiled-firmware-binaries)
  - [Flashing via PlatformIO](#flashing-via-platformio)
  - [Initial Provisioning (Captive Portal)](#step-1-initial-provisioning-captive-portal)
  - [Normal Operation (LAN Dashboard)](#step-2-normal-operation-lan-dashboard)
- [Resource & Thermal Optimization](#-resource--thermal-optimization)
- [Technical Findings & Protocol Gotchas](#-technical-findings--protocol-gotchas)
- [Acknowledgements & Credits](#-acknowledgements--credits)
- [License](#-license)

---

## 🌟 Key Features

1. **Standalone & Zero Hardcode Configuration:**
   - No Wi-Fi credentials or BMS MAC addresses are embedded in the code.
   - On unconfigured first boot (or after Factory Reset), the module starts an open Wi-Fi AP (`JK-BMS-Adapter-Setup`) with automatic DNS Captive Portal redirecting to `http://192.168.4.1/setup`.
2. **Over-The-Air Radio Scanners:**
   - Built-in Wi-Fi network scanner with RSSI signal levels.
   - Built-in Bluetooth Low Energy scanner automatically discovering candidate JK-BMS devices nearby (`service UUID 0xFFE0`).
3. **High-Precision Telemetry:**
   - 1 mV individual cell voltage precision.
   - Automated min/max cell identification and Delta calculation ($\Delta V$).
   - Battery total voltage, signed current (+ Charge / - Discharge), active power in Watts.
   - Coulomb-counter capacity remaining (Ah), State of Charge (SOC %), and full charge capacity.
   - Dual battery temperature probes and MOSFET heat-sink sensor.
   - Real cycle count counter and cumulative cycled throughput (Ah).
4. **Interactive Protection Control:**
   - Direct toggling of **Charge MOS**, **Discharge MOS**, and **Active Balancer** via JK02 holding register commands directly from the browser.
5. **Adaptive Multi-Cell Support (4S / 8S / 16S):**
   - Seamlessly monitor 12V (4S), 24V (8S), or 48V (16S) battery packs.
   - Quick pack configuration selector in the dashboard header adapts the UI on the fly without rebooting.
6. **Dual-Language UI (English / Ukrainian):**
   - English default interface with instant toggle to Ukrainian (`🌐 EN` / `🌐 UA`).
   - Preference is preserved in browser `localStorage`.
7. **Tailscale VPN Remote Access (ESP32-S3):**
   - Secure, direct remote access to the web dashboard and REST API over the internet from anywhere in the world without port forwarding, dynamic DNS, or public IP addresses.
   - Native Tailscale client (ts2021 control protocol, Noise IK mutual authentication, DERP relaying, and WireGuard lwIP netif).
8. **Vector SVG Favicon & HTTP Caching:**
   - Built-in modern SVG lightning badge with 7-day browser caching headers (`Cache-Control: max-age=604800`).
9. **Emergency Hardware Factory Reset:**
   - Holding the onboard **`BOOT` button (GPIO 0)** for **4 seconds** resets NVS settings and safely restarts back to AP configuration mode.

---

## 📐 System Architecture

```
  +-----------------------------------------------------------+
  |              LiFePO4 / Li-Ion Battery Pack                |
  |                (4S 12V / 8S 24V / 16S 48V)                |
  +-----------------------------+-----------------------------+
                                |
                                v
                    +-----------------------+
                    |        JK-BMS         |
                    |    (JK02 Protocol)    |
                    +-----------+-----------+
                                |
                                | Bluetooth 5.0 Low Energy (Service 0xFFE0)
                                | Auto-stream 300-byte telemetry frames
                                v
            +---------------------------------------+
            |      ESP32-S3 Super Mini Adapter      |
            |  * FreeRTOS + NimBLE Client           |
            |  * Async WebServer + REST API         |
            |  * Embedded PROGMEM SPA Web Dashboard |
            +-------------------+-------------------+
                                |
                +---------------+---------------+
                |                               |
                v (Stage 1: AP Mode)            v (Stage 2: STA Mode)
    +-----------------------+       +-----------------------+
    |   SoftAP + Captive    |       |   Home Wi-Fi LAN      |
    |   "JK-BMS-Adapter"    |       |   http://jkbms.local  |
    |   IP: 192.168.4.1     |       |   (or LAN IP address) |
    +-----------+-----------+       +-----------+-----------+
                |                               |
                +---------------+---------------+
                                |
                                v
                +-------------------------------+
                | Smartphone / Tablet / Desktop |
                |       Any Web Browser         |
                +-------------------------------+
```

---

## 🔌 Hardware & Pinout

### ESP32-S3 Super Mini Board
The **ESP32-S3 Super Mini** is an ultra-compact development board featuring the Espressif ESP32-S3 dual-core processor:

| Component | Specification |
| :--- | :--- |
| **MCU** | Espressif ESP32-S3 (Xtensa LX7 32-bit Dual-Core) |
| **Clock Frequency** | Scaled to **160 MHz** (low power, cool operation) |
| **Flash Memory** | 4 MB Embedded XMC Flash (**Mode: DIO**) |
| **PSRAM** | 2 MB Embedded AP_3v3 |
| **SRAM** | 320 KB Internal SRAM |
| **Wireless** | 2.4 GHz Wi-Fi (802.11 b/g/n) + Bluetooth 5.0 BLE |
| **Connector** | USB Type-C (Native USB CDC On-Boot) |

### Physical Button Functions
*(With USB-C port facing UP and components facing you)*

- **LEFT Button (`BOOT` / GPIO 0):**
  - Hold for **4 seconds** $\rightarrow$ **Factory Reset** (wipes NVS Preferences and reboots to AP mode).
- **RIGHT Button (`RST` / EN):**
  - Instant hardware reboot.

---

## 🌐 Bilingual User Interface

The web interface is fully localized in **English (default)** and **Ukrainian**:

- Toggle between languages at any time using the `🌐 EN` / `🌐 UA` button.
- Available in both:
  1. **Main Telemetry Dashboard (`/`)**
  2. **Initial Setup Page (`/setup` / Captive Portal)**
- Browser `localStorage` retains the chosen language across page refreshes.

---

## 🔋 Multi-Cell Pack Support (4S / 8S / 16S)

The adapter dynamically adjusts decoding offsets, calculations, and the visual display based on the selected configuration:

- **4S (12V):** 4 cells (e.g., standard drop-in lead-acid replacement packs).
- **8S (24V):** 8 cells.
- **16S (48V):** 16 cells (e.g., home energy storage / server rack batteries).

### Dynamic CSS Grid
Cards automatically adapt to the screen resolution using CSS Grid auto-fill:
`grid-template-columns: repeat(auto-fill, minmax(130px, 1fr))`

---

## 📡 REST API Specification

All data and control endpoints communicate using lightweight JSON payloads:

| Method | Endpoint | Description | Request Body / Response |
| :--- | :--- | :--- | :--- |
| `GET` | `/` | Web Dashboard (or Setup if unconfigured) | `text/html` |
| `GET` | `/setup` | Explicit Setup page | `text/html` |
| `GET` | `/favicon.ico` | SVG Favicon with cache headers | `image/svg+xml` |
| `GET` | `/api/data` | Real-time live telemetry | JSON |
| `POST`| `/api/set-cells` | Dynamic change of cell count (4, 8, 16) | `{"cells": 8}` |
| `POST`| `/api/switch` | Toggle protection MOSFETs or Balancer | `{"switch":"charging","state":true}` |
| `GET` | `/api/scan-wifi` | Scan 2.4 GHz Wi-Fi networks | `[{"ssid":"...","rssi":-55,"secure":true}]` |
| `GET` | `/api/scan-ble` | Scan Bluetooth air for JK-BMS | `[{"name":"JK-BMS","mac":"...","rssi":-62}]` |
| `POST`| `/api/save-config`| Save credentials & reboot | `{"ssid":"...","pass":"...","mac":"...","cells":4}` |
| `POST`| `/api/reset-wifi` | Wipe NVS and reboot to AP | - |
| `POST`| `/update` | Web OTA multipart firmware binary upload | `multipart/form-data ("firmware")` |

### Sample Telemetry Response (`GET /api/data`):
```json
{
  "connected": true,
  "total_voltage": 13.301,
  "current": 0.0,
  "power": 0.0,
  "charge_power": 0.0,
  "discharge_power": 0.0,
  "soc": 89,
  "capacity_remain": 135.085,
  "cycle_count": 1,
  "cycle_capacity": 302.929,
  "cell_count": 4,
  "cells": [3.325, 3.325, 3.326, 3.325],
  "min_cell_idx": 1,
  "max_cell_idx": 3,
  "delta_cell_v": 0.001,
  "temp_mos": 21.1,
  "temp_sensor1": 20.0,
  "temp_sensor2": 20.3,
  "balancing_active": false,
  "balancing_current": 0.0,
  "balancer_direction": "Idle",
  "switch_charging": true,
  "switch_discharging": true,
  "switch_balancer": true,
  "errors": "OK"
}
```

---

## 🚀 Getting Started

### Pre-compiled Firmware Binaries

If you prefer not to compile from source, pre-built binary images are available on the [GitHub Releases page](https://github.com/tigersumy/JK-BMS-Smart-Monitor-ESP32-s3/releases):

| Firmware Flavor | Factory Image (`0x0000`) | App Only (`0x10000` / OTA) | Description |
| :--- | :--- | :--- | :--- |
| **ESP32-S3 + Tailscale VPN** ⭐ | [`jkbms_esp32s3_tailscale_firmware_factory.bin`](https://github.com/tigersumy/JK-BMS-Smart-Monitor-ESP32-s3/releases/download/v1.0.0/jkbms_esp32s3_tailscale_firmware_factory.bin) | [`jkbms_esp32s3_tailscale_firmware.bin`](https://github.com/tigersumy/JK-BMS-Smart-Monitor-ESP32-s3/releases/download/v1.0.0/jkbms_esp32s3_tailscale_firmware.bin) | **Recommended.** Built-in remote VPN access & 2MB PSRAM buffers. |
| **ESP32-S3 (Standard)** | [`jkbms_esp32s3_firmware_factory.bin`](https://github.com/tigersumy/JK-BMS-Smart-Monitor-ESP32-s3/releases/download/v1.0.0/jkbms_esp32s3_firmware_factory.bin) | [`jkbms_esp32s3_firmware.bin`](https://github.com/tigersumy/JK-BMS-Smart-Monitor-ESP32-s3/releases/download/v1.0.0/jkbms_esp32s3_firmware.bin) | Standard LAN-only build without Tailscale client. |
| **ESP32-C6 (Wi-Fi 6)** | [`jkbms_esp32c6_firmware_factory.bin`](https://github.com/tigersumy/JK-BMS-Smart-Monitor-ESP32-s3/releases/download/v1.0.0/jkbms_esp32c6_firmware_factory.bin) | [`jkbms_esp32c6_firmware.bin`](https://github.com/tigersumy/JK-BMS-Smart-Monitor-ESP32-s3/releases/download/v1.0.0/jkbms_esp32c6_firmware.bin) | RISC-V 160 MHz + Wi-Fi 6 target. |

#### Quick Flashing:
- **Web Browser (Easiest):** Open [ESP Web Tools](https://espressif.github.io/esptool-js/) in Google Chrome or Microsoft Edge, plug in the board via USB-C, and flash the factory binary at offset `0x0000`.
- **Using `esptool.py` CLI:**
  ```bash
  # Flash ESP32-S3 with Tailscale VPN:
  esptool.py -b 921600 write_flash 0x0 jkbms_esp32s3_tailscale_firmware_factory.bin
  ```
- **Web OTA over Network (No USB cable needed for already running devices):**
  ```bash
  curl -F "firmware=@jkbms_esp32s3_tailscale_firmware.bin" http://jkbms.local/update
  # or via Tailscale IP / MagicDNS:
  curl -F "firmware=@jkbms_esp32s3_tailscale_firmware.bin" http://jkbms-esp32.your-tailnet.ts.net/update
  ```

---

### Flashing via PlatformIO

#### Prerequisites
- [Visual Studio Code](https://code.visualstudio.com/) + [PlatformIO IDE extension](https://platformio.org/platformio-ide)
- Or PlatformIO Core CLI (`pio`).

#### Build and Upload:
1. **Clone the repository:**
   ```bash
   git clone https://github.com/tigersumy/JK-BMS-Smart-Monitor-ESP32-s3.git
   cd JK-BMS-Smart-Monitor-ESP32-s3
   ```

2. **Connect the ESP32-S3 Super Mini** to your computer via USB-C.

3. **Build and upload firmware:**
   ```bash
   pio run -t upload
   ```

4. **Monitor serial logs (optional):**
   ```bash
   pio device monitor
   ```

---

## 📱 Operating Guide

### Step 1: Initial Provisioning (Captive Portal)
1. On initial power-up, the ESP32-S3 creates an open Wi-Fi network:
   - **SSID:** `JK-BMS-Adapter-Setup` (Open, no password).
2. Connect using your phone, tablet, or laptop.
3. The browser will automatically open the setup page (or navigate to `http://192.168.4.1/setup`).
4. Click **🔄 Scan** to pick your home Wi-Fi SSID and enter your Wi-Fi password.
5. Click **🔍 Find BMS** to detect nearby JK-BMS Bluetooth MAC addresses, or enter it manually.
6. Select your battery configuration (**4S, 8S, or 16S**).
7. Click **💾 Save & Connect**. The ESP32-S3 saves the settings into NVS and reboots.

### Step 2: Normal Operation (LAN Dashboard)
1. Connect to your home Wi-Fi network.
2. Open your web browser and go to:
   - **mDNS address:** `http://jkbms.local`
   - Or direct IP assigned by your router (e.g., `http://192.168.1.150`).
3. View real-time voltages, temperatures, SOC, and toggle switches directly from the page.

---

## ⚡ Resource & Thermal Optimization

To ensure continuous, cool, 24/7 standalone operation:
- **CPU Clock Frequency:** Reduced from 240 MHz to **160 MHz** (`board_build.f_cpu = 160000000L`). This cuts thermal output significantly while retaining plenty of performance.
- **Modem Sleep:** `WiFi.setSleep(WIFI_PS_MIN_MODEM)` enables radio sleep intervals between DTIM beacons.
- **Current Draw:** ~60 mA in steady monitoring state.
- **Memory Footprint:**
  - **RAM:** ~52 KB used (15.9%) / ~275 KB free SRAM.
  - **Flash:** ~992 KB used (75%) / 1.3 MB partition.

---

## 🌐 Tailscale VPN Remote Access (ESP32-S3)

The ESP32-S3 firmware includes an embedded Tailscale VPN client based on [MicroLink](https://github.com/CamM2325/microlink) and WireGuard lwIP:
- **Zero-Configuration NAT Traversal:** Connects to your Tailscale network (*tailnet*) over standard Wi-Fi, establishing direct encrypted P2P WireGuard sessions (or fallback via DERP relay).
- **Direct Remote Access & MagicDNS:** Allows opening the dashboard and querying the REST API from anywhere in the world using MagicDNS (e.g. `http://jkbms-esp32.your-tailnet.ts.net/`) or direct Tailscale IP (e.g. `http://100.x.y.z/`).
- **Large Tailnet Support (Up to 32 Peers):** Supports up to 32 simultaneous peers in your tailnet (`CONFIG_ML_MAX_PEERS=32` and `WIREGUARD_MAX_PEERS=32`), preventing peer dropouts on busy networks.
- **Memory Efficient:** Large buffers (HTTP/2, JSON tree, WireGuard state) are dynamically allocated in the 2 MB PSRAM, preserving internal SRAM.
- **Easy Provisioning:** Tailscale Auth Key and hostname can be entered directly from the `/setup` captive portal or configuration page.

---

## 🔍 Technical Findings & Protocol Gotchas

- **JK-BMS BLE GATT Handles:**
  Older modules (`C8:47:80:...`) require register writes to be sent to **`0xFFE2`** (`Write Without Response`) or `0xFFE1`.
- **Handshake Sequence:**
  Sending `0x96` (Cell Info) in a tight loop blocks the BMS. The correct handshake sends `0x97` (Device Info) once upon connection, followed by `0x96` once; the BMS then streams continuous `0x02` 300-byte telemetry packets.
- **Cycle Count vs Cycled Capacity:**
  - Byte `150 + offset` holds **`Cycle_Count`** (integer full cycles).
  - Byte `154 + offset` holds **`Cycle_Capacity`** in milliampere-hours ($0.001\text{ Ah}$ total throughput).

---

## 🤝 Acknowledgements & Credits

This project builds upon and integrates several exceptional open-source libraries:
- **[MicroLink by CamM2325](https://github.com/CamM2325/microlink):** The embedded Tailscale client implementation for ESP32 (ts2021 control protocol, Noise IK mutual authentication, DERP relay protocol, and magicsock architecture) is adapted from the MicroLink project.
- **[wireguard-lwip by Daniel Hope](https://github.com/djp952/wireguard-lwip):** Lightweight WireGuard implementation for lwIP providing ChaCha20-Poly1305, BLAKE2s, and Curve25519 cryptographic primitives.
- **[NimBLE-Arduino by h2zero](https://github.com/h2zero/NimBLE-Arduino):** Memory-efficient Bluetooth Low Energy stack for ESP32.
- **[ArduinoJson by Benoît Blanchon](https://arduinojson.org/):** High-performance JSON parser for embedded systems.

---

## 📄 License

This project is licensed under the [MIT License](LICENSE).
Feel free to use, modify, and integrate into your home energy systems!
