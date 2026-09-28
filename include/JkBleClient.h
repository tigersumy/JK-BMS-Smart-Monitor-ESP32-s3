#pragma once

#include <Arduino.h>
#include <NimBLEDevice.h>
#include <vector>
#include "JkBmsProtocol.h"

struct BleDiscoveredDevice {
    String name;
    String mac;
    int rssi;
};

class JkBleClient : public NimBLEClientCallbacks {
public:
    JkBleClient();
    bool init();
    void setTargetMac(const String& mac);
    void setCellCount(uint8_t count);
    bool connect();
    void disconnect();
    bool isConnected();
    void loop();

    bool setSwitch(uint8_t reg, bool state);
    const JkTelemetry& getTelemetry() const { return telemetry_; }

    static std::vector<BleDiscoveredDevice> scanForBms(uint32_t durationSec = 4);

    // NimBLE Callbacks
    void onConnect(NimBLEClient* pClient) override;
    void onDisconnect(NimBLEClient* pClient) override;

private:
    void sendCellInfoRequest();
    void assemble(const uint8_t* data, size_t length);
    void decodeFrame(const std::vector<uint8_t>& data);
    void decodeCellInfo(const std::vector<uint8_t>& data);
    void decodeSettings(const std::vector<uint8_t>& data);

    static uint8_t calcCrc(const uint8_t* data, size_t len);
    static std::vector<uint8_t> buildFrame(uint8_t address, uint32_t value, uint8_t length);

    String targetMac_;
    NimBLEClient* pClient_ = nullptr;
    NimBLERemoteCharacteristic* pCharWrite_ = nullptr;
    NimBLERemoteCharacteristic* pCharNotify_ = nullptr;
    
    JkTelemetry telemetry_;
    std::vector<uint8_t> rxBuffer_;
    uint32_t lastPollTime_ = 0;
    uint32_t lastReconnectAttempt_ = 0;
    bool isConnecting_ = false;

    static void notifyCallback(NimBLERemoteCharacteristic* pBLERemoteCharacteristic,
                               uint8_t* pData, size_t length, bool isNotify);
};
