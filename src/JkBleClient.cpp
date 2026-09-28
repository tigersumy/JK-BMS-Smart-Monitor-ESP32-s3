#include "JkBleClient.h"

static JkBleClient* s_instance = nullptr;

JkBleClient::JkBleClient() {
    s_instance = this;
}

bool JkBleClient::init() {
    NimBLEDevice::init("ESP32S3-JKBMS");
    NimBLEDevice::setPower(ESP_PWR_LVL_P9);
    NimBLEDevice::setSecurityAuth(false, false, false);
    return true;
}

void JkBleClient::setTargetMac(const String& mac) {
    targetMac_ = mac;
    targetMac_.trim();
    targetMac_.toUpperCase();
}

void JkBleClient::setCellCount(uint8_t count) {
    if (count == 4 || count == 8 || count == 16) {
        telemetry_.cell_count = count;
        Serial.printf("[BLE] Configured for %dS cell configuration\n", count);
    }
}

void JkBleClient::onConnect(NimBLEClient* pClient) {
    Serial.println("[BLE] Connected to JK-BMS successfully");
}

void JkBleClient::onDisconnect(NimBLEClient* pClient) {
    Serial.println("[BLE] Disconnected from JK-BMS");
    telemetry_.connected = false;
    pCharWrite_ = nullptr;
    pCharNotify_ = nullptr;
}

bool JkBleClient::connect() {
    if (targetMac_.length() < 12) {
        return false;
    }

    if (pClient_ && pClient_->isConnected()) {
        return true;
    }

    Serial.printf("[BLE] Connecting to JK-BMS at [%s]...\n", targetMac_.c_str());
    isConnecting_ = true;

    if (!pClient_) {
        pClient_ = NimBLEDevice::createClient();
        pClient_->setClientCallbacks(this, false);
        pClient_->setConnectionParams(12, 12, 0, 200); // Fast connection
        pClient_->setConnectTimeout(5);
    }

    NimBLEAddress bleAddr(targetMac_.c_str());
    if (!pClient_->connect(bleAddr, false)) {
        Serial.println("[BLE] Failed to connect to peripheral");
        isConnecting_ = false;
        return false;
    }

    NimBLERemoteService* pSvc = pClient_->getService(JK_SERVICE_UUID);
    if (!pSvc) {
        Serial.println("[BLE] Target service 0xFFE0 not found!");
        pClient_->disconnect();
        isConnecting_ = false;
        return false;
    }

    Serial.println("[BLE] Discovered characteristics in 0xFFE0:");
    pCharWrite_ = nullptr;
    pCharNotify_ = nullptr;

    auto* chars = pSvc->getCharacteristics(true);
    if (chars) {
        for (auto* c : *chars) {
            String uuid = c->getUUID().toString().c_str();
            uuid.toLowerCase();
            Serial.printf("[BLE]   UUID: %s, handle: %d, canNotify: %d, canWrite: %d, canWriteNoResp: %d\n",
                          uuid.c_str(), c->getHandle(), 
                          c->canNotify(), c->canWrite(), c->canWriteNoResponse());

            if (uuid.indexOf("ffe2") >= 0 || (c->canWriteNoResponse() && !c->canNotify())) {
                pCharWrite_ = c;
                Serial.printf("[BLE]   Selected write characteristic: handle %d\n", c->getHandle());
            }

            if (uuid.indexOf("ffe1") >= 0 || c->canNotify()) {
                pCharNotify_ = c;
                c->subscribe(true, notifyCallback);
                Serial.printf("[BLE]   Subscribed to notify on handle %d\n", c->getHandle());
            }
        }
    }

    if (!pCharWrite_ && pCharNotify_) {
        pCharWrite_ = pCharNotify_;
    }

    telemetry_.connected = true;
    isConnecting_ = false;
    rxBuffer_.clear();

    // 1. Request device info (0x97) to handshake session
    Serial.println("[BLE] Requesting DeviceInfo (0x97)...");
    auto frameDev = buildFrame(0x97, 0, 0);
    pCharNotify_->writeValue(frameDev.data(), frameDev.size(), false);

    delay(300);

    // 2. Request cell info (0x96)
    sendCellInfoRequest();
    return true;
}

void JkBleClient::disconnect() {
    if (pClient_ && pClient_->isConnected()) {
        pClient_->disconnect();
    }
    telemetry_.connected = false;
    pCharWrite_ = nullptr;
    pCharNotify_ = nullptr;
}

bool JkBleClient::isConnected() {
    return (pClient_ && pClient_->isConnected() && telemetry_.connected);
}

void JkBleClient::notifyCallback(NimBLERemoteCharacteristic* pChar, uint8_t* pData, size_t length, bool isNotify) {
    if (s_instance) {
        s_instance->assemble(pData, length);
    }
}

uint8_t JkBleClient::calcCrc(const uint8_t* data, size_t len) {
    uint8_t c = 0;
    for (size_t i = 0; i < len; i++) {
        c += data[i];
    }
    return c;
}

std::vector<uint8_t> JkBleClient::buildFrame(uint8_t address, uint32_t value, uint8_t length) {
    std::vector<uint8_t> frame(20, 0x00);
    frame[0] = 0xAA;
    frame[1] = 0x55;
    frame[2] = 0x90;
    frame[3] = 0xEB;
    frame[4] = address;
    frame[5] = length;
    frame[6] = (value >> 0) & 0xFF;
    frame[7] = (value >> 8) & 0xFF;
    frame[8] = (value >> 16) & 0xFF;
    frame[9] = (value >> 24) & 0xFF;
    frame[19] = calcCrc(frame.data(), 19);
    return frame;
}

bool JkBleClient::setSwitch(uint8_t reg, bool state) {
    if (!isConnected() || !pCharWrite_) {
        return false;
    }
    uint32_t val = state ? 1 : 0;
    auto frame = buildFrame(reg, val, 4);
    Serial.printf("[BLE] Setting register %u to %d\n", reg, state ? 1 : 0);
    return pCharWrite_->writeValue(frame.data(), frame.size(), false);
}

void JkBleClient::sendCellInfoRequest() {
    if (!isConnected() || !pCharNotify_) return;
    auto frame = buildFrame(0x96, 0, 0);
    bool ok = pCharNotify_->writeValue(frame.data(), frame.size(), false);
    Serial.printf("[BLE] Sent CellInfo request (0x96), status: %d\n", ok);
}

void JkBleClient::assemble(const uint8_t* data, size_t length) {
    if (rxBuffer_.size() > 500) {
        rxBuffer_.clear();
    }
    // If packet starts with JK response header 0x55 0xAA 0xEB 0x90, reset buffer
    if (length >= 4 && data[0] == 0x55 && data[1] == 0xAA && data[2] == 0xEB && data[3] == 0x90) {
        rxBuffer_.clear();
    }

    rxBuffer_.insert(rxBuffer_.end(), data, data + length);

    // Frame complete check: JK02 frames are typically 300 bytes
    if (rxBuffer_.size() >= 300) {
        decodeFrame(rxBuffer_);
        rxBuffer_.clear();
    }
}

void JkBleClient::decodeFrame(const std::vector<uint8_t>& data) {
    if (data.size() < 300) return;
    if (data[0] != 0x55 || data[1] != 0xAA || data[2] != 0xEB || data[3] != 0x90) {
        return;
    }

    uint8_t frameType = data[4];
    if (frameType == 0x01) {
        decodeSettings(data);
    } else if (frameType == 0x02) {
        decodeCellInfo(data);
    }
}

void JkBleClient::decodeCellInfo(const std::vector<uint8_t>& data) {
    auto get16 = [&](size_t i) -> uint16_t {
        if (i + 1 >= data.size()) return 0;
        return (uint16_t(data[i + 1]) << 8) | uint16_t(data[i]);
    };
    auto get32 = [&](size_t i) -> uint32_t {
        return (uint32_t(get16(i + 2)) << 16) | uint32_t(get16(i));
    };

    // Cell Voltages (bytes 6 + i * 2) for configurable cell count (4, 8, 16)
    uint8_t count = telemetry_.cell_count;
    if (count != 4 && count != 8 && count != 16) count = 4;

    float minV = 999.0f;
    float maxV = 0.0f;
    uint8_t minIdx = 1;
    uint8_t maxIdx = 1;

    for (int i = 0; i < count && i < 16; i++) {
        float v = (float)get16(6 + i * 2) * 0.001f;
        telemetry_.cell_voltages[i] = v;
        if (v > 0.5f) {
            if (v < minV) {
                minV = v;
                minIdx = i + 1;
            }
            if (v > maxV) {
                maxV = v;
                maxIdx = i + 1;
            }
        }
    }

    telemetry_.min_cell_v = (minV < 900.0f) ? minV : 0.0f;
    telemetry_.max_cell_v = maxV;
    telemetry_.delta_cell_v = (maxV > minV) ? (maxV - minV) : 0.0f;
    telemetry_.min_cell_idx = minIdx;
    telemetry_.max_cell_idx = maxIdx;

    // Detect 32S offset vs 24S offset:
    // In JK02_32S total voltage is at 118 + 32 = 150.
    // In JK02_24S total voltage is at 118.
    size_t offset = 0;
    float v32 = (float)get32(150) * 0.001f;
    float v24 = (float)get32(118) * 0.001f;
    if (v32 >= 8.0f && v32 <= 80.0f) {
        offset = 32; // JK02_32S protocol
    } else if (v24 >= 8.0f && v24 <= 80.0f) {
        offset = 0;  // JK02_24S protocol
    } else {
        offset = 32; // Default to 32S
    }

    telemetry_.total_voltage = (float)get32(118 + offset) * 0.001f;

    // Current (signed 32-bit: positive = charge, negative = discharge)
    int32_t rawCurrent = (int32_t)get32(126 + offset);
    telemetry_.current = (float)rawCurrent * 0.001f;
    telemetry_.power = telemetry_.total_voltage * telemetry_.current;
    if (telemetry_.power >= 0) {
        telemetry_.charge_power = telemetry_.power;
        telemetry_.discharge_power = 0.0f;
    } else {
        telemetry_.charge_power = 0.0f;
        telemetry_.discharge_power = -telemetry_.power;
    }

    // Temperatures
    telemetry_.temp_sensor1 = (float)((int16_t)get16(130 + offset)) * 0.1f;
    telemetry_.temp_sensor2 = (float)((int16_t)get16(132 + offset)) * 0.1f;
    if (offset == 32) {
        telemetry_.temp_mos = (float)((int16_t)get16(112 + offset)) * 0.1f;
    } else {
        telemetry_.temp_mos = (float)((int16_t)get16(134 + offset)) * 0.1f;
    }

    // Balancer
    telemetry_.balancing_current = (float)((int16_t)get16(138 + offset)) * 0.001f;
    uint8_t balState = (140 + offset < data.size()) ? data[140 + offset] : 0;
    telemetry_.balancing_active = (balState != 0);
    if (balState == 1) telemetry_.balancer_direction = "Заряд осередку";
    else if (balState == 2) telemetry_.balancer_direction = "Розряд осередку";
    else telemetry_.balancer_direction = "Очікування";

    // SOC & Capacity
    if (141 + offset < data.size()) {
        telemetry_.soc = (float)data[141 + offset];
    }
    telemetry_.capacity_remain = (float)get32(142 + offset) * 0.001f;

    // Cycle Count & Cumulative Cycled Capacity
    telemetry_.cycle_count    = get32(150 + offset);
    telemetry_.cycle_capacity = (float)get32(154 + offset) * 0.001f;

    // Errors bitmask
    uint32_t errs = get32(134 + offset);
    telemetry_.raw_errors = errs;
    telemetry_.errors_str = (errs == 0) ? "OK (Без помилок)" : ("0x" + String(errs, HEX));

    telemetry_.last_update = millis();

    static uint32_t lastPrint = 0;
    if (millis() - lastPrint > 30000) {
        lastPrint = millis();
        Serial.printf("[BLE] Telemetry (%dS): V_tot=%.2fV, I=%.2fA, SOC=%.0f%%, Delta=%.3fV (Min=C%d:%.3fV, Max=C%d:%.3fV)\n",
                      count,
                      telemetry_.total_voltage, telemetry_.current, telemetry_.soc,
                      telemetry_.delta_cell_v,
                      telemetry_.min_cell_idx, telemetry_.min_cell_v,
                      telemetry_.max_cell_idx, telemetry_.max_cell_v);
    }
}

void JkBleClient::decodeSettings(const std::vector<uint8_t>& data) {
    if (data.size() < 130) return;
    // Charge switch at 118, Discharge switch at 122, Balancer switch at 126
    telemetry_.switch_charging    = (data[118] != 0);
    telemetry_.switch_discharging = (data[122] != 0);
    telemetry_.switch_balancer    = (data[126] != 0);
}

void JkBleClient::loop() {
    uint32_t now = millis();

    // Auto reconnect if configured
    if (!isConnected() && targetMac_.length() >= 12 && !isConnecting_) {
        if (now - lastReconnectAttempt_ > 5000) {
            lastReconnectAttempt_ = now;
            connect();
        }
    }

    // Only request telemetry if connected and no data received for 10 seconds
    if (isConnected()) {
        if (telemetry_.last_update == 0 || (now - telemetry_.last_update > 10000)) {
            if (now - lastPollTime_ > 5000) {
                lastPollTime_ = now;
                Serial.println("[BLE] Requesting CellInfo from BMS...");
                sendCellInfoRequest();
            }
        }
    }
}

std::vector<BleDiscoveredDevice> JkBleClient::scanForBms(uint32_t durationSec) {
    std::vector<BleDiscoveredDevice> results;
    NimBLEScan* pScan = NimBLEDevice::getScan();
    pScan->setActiveScan(true);
    pScan->setInterval(97);
    pScan->setWindow(67);
    
    Serial.println("[BLE] Starting BLE scan for JK-BMS...");
    NimBLEScanResults scanResults = pScan->start(durationSec, false);
    
    for (int i = 0; i < scanResults.getCount(); i++) {
        NimBLEAdvertisedDevice dev = scanResults.getDevice(i);
        String name = dev.getName().c_str();
        String mac = dev.getAddress().toString().c_str();
        int rssi = dev.getRSSI();

        // Check if name contains JK or service UUID is present
        if (name.indexOf("JK") >= 0 || name.indexOf("BMS") >= 0 || dev.isAdvertisingService(NimBLEUUID("ffe0"))) {
            BleDiscoveredDevice item;
            item.name = name.length() > 0 ? name : "JK-BMS Unknown";
            item.mac = mac;
            item.rssi = rssi;
            results.push_back(item);
        }
    }
    pScan->clearResults();
    Serial.printf("[BLE] Scan complete. Found %d BMS candidate(s)\n", results.size());
    return results;
}
