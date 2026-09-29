#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <ArduinoJson.h>

#include "Config.h"
#include "JkBmsProtocol.h"
#include "JkBleClient.h"
#include "WebDashboard.h"

// Hardware Pin for BOOT button (ESP32-C6 uses GPIO 9; ESP32-S3 uses GPIO 0)
#if defined(CONFIG_IDF_TARGET_ESP32C6)
static const uint8_t BOOT_BUTTON_PIN = 9;
#else
static const uint8_t BOOT_BUTTON_PIN = 0;
#endif

// Network & WebServer
static const byte DNS_PORT = 53;
static DNSServer dnsServer;
static WebServer server(80);

static AppConfig currentConfig;
static JkBleClient bleClient;
static bool isApMode = false;
static uint32_t bootPressStart = 0;

// Captive Portal detection
bool isCaptivePortalRequest() {
    if (!isApMode) return false;
    String host = server.hostHeader();
    if (host.length() == 0 || host.indexOf("192.168.4.1") >= 0) {
        return false;
    }
    return true;
}

void setupWebServerRoutes() {
    // Main Dashboard or Setup
    server.on("/", HTTP_GET, []() {
        Serial.println("[HTTP] Client connected to /");
        if (isApMode || currentConfig.wifi_ssid.length() == 0) {
            server.send_P(200, "text/html", SETUP_HTML);
        } else {
            server.send_P(200, "text/html", INDEX_HTML);
        }
    });

    server.on("/setup", HTTP_GET, []() {
        server.send_P(200, "text/html", SETUP_HTML);
    });

    // Favicon (SVG badge with cache header)
    auto sendFavicon = []() {
        server.sendHeader("Cache-Control", "public, max-age=604800");
        server.send_P(200, "image/svg+xml", FAVICON_SVG);
    };
    server.on("/favicon.ico", HTTP_GET, sendFavicon);
    server.on("/favicon.svg", HTTP_GET, sendFavicon);

    // API: Live Telemetry Data
    server.on("/api/data", HTTP_GET, []() {
        const auto& t = bleClient.getTelemetry();
        JsonDocument doc;
        doc["connected"] = bleClient.isConnected();
        doc["total_voltage"] = t.total_voltage;
        doc["current"] = t.current;
        doc["power"] = t.power;
        doc["charge_power"] = t.charge_power;
        doc["discharge_power"] = t.discharge_power;
        doc["soc"] = t.soc;
        doc["capacity_remain"] = t.capacity_remain;
        doc["cycle_count"] = t.cycle_count;
        doc["cycle_capacity"] = t.cycle_capacity;
        doc["cell_count"] = t.cell_count;

        JsonArray cells = doc["cells"].to<JsonArray>();
        for (int i = 0; i < t.cell_count && i < 16; i++) {
            cells.add(t.cell_voltages[i]);
        }
        doc["min_cell_idx"] = t.min_cell_idx;
        doc["max_cell_idx"] = t.max_cell_idx;
        doc["delta_cell_v"] = t.delta_cell_v;

        doc["temp_mos"] = t.temp_mos;
        doc["temp_sensor1"] = t.temp_sensor1;
        doc["temp_sensor2"] = t.temp_sensor2;

        doc["balancing_active"] = t.balancing_active;
        doc["balancing_current"] = t.balancing_current;
        doc["balancer_direction"] = t.balancer_direction;

        doc["switch_charging"] = t.switch_charging;
        doc["switch_discharging"] = t.switch_discharging;
        doc["switch_balancer"] = t.switch_balancer;

        doc["errors"] = t.errors_str;

        String out;
        serializeJson(doc, out);
        server.send(200, "application/json", out);
    });

    // API: Scan Wi-Fi Networks
    server.on("/api/scan-wifi", HTTP_GET, []() {
        Serial.println("[WIFI] Scanning networks...");
        int n = WiFi.scanNetworks(false, false);
        JsonDocument doc;
        JsonArray arr = doc.to<JsonArray>();

        for (int i = 0; i < n; i++) {
            JsonObject item = arr.add<JsonObject>();
            item["ssid"] = WiFi.SSID(i);
            item["rssi"] = WiFi.RSSI(i);
            item["secure"] = (WiFi.encryptionType(i) != WIFI_AUTH_OPEN);
        }
        WiFi.scanDelete();

        String out;
        serializeJson(doc, out);
        server.send(200, "application/json", out);
    });

    // API: Scan BLE for JK-BMS devices
    server.on("/api/scan-ble", HTTP_GET, []() {
        auto list = JkBleClient::scanForBms(4);
        JsonDocument doc;
        JsonArray arr = doc.to<JsonArray>();

        for (const auto& item : list) {
            JsonObject obj = arr.add<JsonObject>();
            obj["name"] = item.name;
            obj["mac"]  = item.mac;
            obj["rssi"] = item.rssi;
        }

        String out;
        serializeJson(doc, out);
        server.send(200, "application/json", out);
    });

    // API: Toggle BMS Switches
    server.on("/api/switch", HTTP_POST, []() {
        if (!server.hasArg("plain")) {
            server.send(400, "application/json", "{\"error\":\"Missing body\"}");
            return;
        }
        JsonDocument doc;
        DeserializationError err = deserializeJson(doc, server.arg("plain"));
        if (err) {
            server.send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
            return;
        }

        const char* sw = doc["switch"];
        bool state = doc["state"];

        bool ok = false;
        if (strcmp(sw, "charging") == 0) {
            ok = bleClient.setSwitch(JK02_REG_CHARGE_SWITCH, state);
        } else if (strcmp(sw, "discharging") == 0) {
            ok = bleClient.setSwitch(JK02_REG_DISCHARGE_SWITCH, state);
        } else if (strcmp(sw, "balancer") == 0) {
            ok = bleClient.setSwitch(JK02_REG_BALANCER_SWITCH, state);
        }

        if (ok) {
            server.send(200, "application/json", "{\"status\":\"ok\"}");
        } else {
            server.send(500, "application/json", "{\"status\":\"error\"}");
        }
    });

    // API: Save Configuration
    server.on("/api/save-config", HTTP_POST, []() {
        if (!server.hasArg("plain")) {
            server.send(400, "application/json", "{\"error\":\"Missing body\"}");
            return;
        }
        JsonDocument doc;
        deserializeJson(doc, server.arg("plain"));

        AppConfig cfg;
        cfg.wifi_ssid  = doc["ssid"].as<String>();
        cfg.wifi_pass  = doc["pass"].as<String>();
        cfg.bms_mac    = doc["mac"].as<String>();
        cfg.bms_pin    = doc["pin"].as<String>();
        cfg.cell_count = doc["cells"] | 4;
        if (cfg.cell_count != 4 && cfg.cell_count != 8 && cfg.cell_count != 16) {
            cfg.cell_count = 4;
        }

        ConfigManager::save(cfg);
        server.send(200, "application/json", "{\"status\":\"saved\"}");

        Serial.println("[CONFIG] Saved new configuration. Rebooting in 1s...");
        delay(1000);
        ESP.restart();
    });

    // API: Quick change cell count without full reboot or Wi-Fi reset
    server.on("/api/set-cells", HTTP_POST, []() {
        if (!server.hasArg("plain")) {
            server.send(400, "application/json", "{\"error\":\"Missing body\"}");
            return;
        }
        JsonDocument doc;
        deserializeJson(doc, server.arg("plain"));
        uint8_t count = doc["cells"] | 4;
        if (count == 4 || count == 8 || count == 16) {
            currentConfig.cell_count = count;
            ConfigManager::save(currentConfig);
            bleClient.setCellCount(count);
            server.send(200, "application/json", "{\"status\":\"ok\",\"cells\":" + String(count) + "}");
            Serial.printf("[CONFIG] Cell count updated to %dS\n", count);
        } else {
            server.send(400, "application/json", "{\"error\":\"Invalid cell count (4, 8, or 16)\"}");
        }
    });

    // Reset Wi-Fi and reboot to AP
    server.on("/api/reset-wifi", HTTP_POST, []() {
        ConfigManager::clear();
        server.send(200, "application/json", "{\"status\":\"reset\"}");
        delay(1000);
        ESP.restart();
    });

    // Captive Portal probe catch-all
    server.onNotFound([]() {
        if (isCaptivePortalRequest()) {
            server.sendHeader("Location", "http://192.168.4.1/setup", true);
            server.send(302, "text/plain", "");
        } else {
            server.send(404, "text/plain", "Not Found");
        }
    });
}

void startApMode() {
    isApMode = true;
    WiFi.mode(WIFI_AP);
    WiFi.softAP("JK-BMS-Adapter-Setup"); // Open Wi-Fi AP, no password

    IPAddress apIP(192, 168, 4, 1);
    WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));

    dnsServer.start(DNS_PORT, "*", apIP);
    Serial.println("[WIFI] Started AP Mode: 'JK-BMS-Adapter-Setup' (192.168.4.1)");
}

void checkBootButton() {
    // GPIO 0 is the BOOT button on ESP32-S3 Super Mini (active LOW)
    if (digitalRead(BOOT_BUTTON_PIN) == LOW) {
        if (bootPressStart == 0) {
            bootPressStart = millis();
            Serial.println("[BOOT] Button pressed, hold 4s for factory reset...");
        } else if (millis() - bootPressStart > 4000) {
            Serial.println("[BOOT] Held > 4s! Factory resetting configuration to AP mode...");
            ConfigManager::clear();
            delay(300);
            ESP.restart();
        }
    } else {
        if (bootPressStart != 0) {
            bootPressStart = 0;
            Serial.println("[BOOT] Button released");
        }
    }
}

void setup() {
    Serial.begin(115200);
    delay(1500);
    Serial.println("\n=========================================");
#if defined(CONFIG_IDF_TARGET_ESP32C6)
    Serial.println("  JK-BMS Universal Adapter (ESP32-C6)   ");
#else
    Serial.println("  JK-BMS Universal Adapter (ESP32-S3)   ");
#endif
    Serial.println("=========================================");

    pinMode(BOOT_BUTTON_PIN, INPUT_PULLUP);

    currentConfig = ConfigManager::load();
    Serial.printf("[CONFIG] Active SSID: '%s', BMS MAC: '%s', Cells: %dS\n", 
                  currentConfig.wifi_ssid.c_str(), currentConfig.bms_mac.c_str(), currentConfig.cell_count);

    // Initialize BLE client
    bleClient.init();
    bleClient.setCellCount(currentConfig.cell_count);
    if (currentConfig.bms_mac.length() >= 12) {
        bleClient.setTargetMac(currentConfig.bms_mac);
    }

    // Wi-Fi Connection
    if (currentConfig.wifi_ssid.length() > 0) {
        WiFi.mode(WIFI_STA);
        WiFi.begin(currentConfig.wifi_ssid.c_str(), currentConfig.wifi_pass.c_str());
        Serial.printf("[WIFI] Connecting to '%s'...", currentConfig.wifi_ssid.c_str());

        uint32_t startAttempt = millis();
        while (WiFi.status() != WL_CONNECTED && millis() - startAttempt < 15000) {
            delay(500);
            Serial.print(".");
        }
        Serial.println();

        if (WiFi.status() == WL_CONNECTED) {
            WiFi.setSleep(WIFI_PS_MIN_MODEM);
            Serial.printf("[WIFI] Connected! IP: %s\n", WiFi.localIP().toString().c_str());
            if (MDNS.begin("jkbms")) {
                MDNS.addService("http", "tcp", 80);
                Serial.println("[MDNS] Responding at http://jkbms.local");
            }
        } else {
            Serial.println("[WIFI] Connection failed. Falling back to AP mode.");
            startApMode();
        }
    } else {
        Serial.println("[WIFI] No SSID configured. Starting AP mode.");
        startApMode();
    }

    setupWebServerRoutes();
    server.begin();
    Serial.println("[HTTP] WebServer started on port 80");
}

void loop() {
    checkBootButton();

    if (isApMode) {
        dnsServer.processNextRequest();
    }

    server.handleClient();
    bleClient.loop();

    static uint32_t lastHb = 0;
    if (millis() - lastHb > 60000) {
        lastHb = millis();
        Serial.printf("[STATUS] IP: %s | Free Heap: %d KB | BLE: %s\n", 
                      WiFi.localIP().toString().c_str(), 
                      ESP.getFreeHeap() / 1024,
                      bleClient.isConnected() ? "Connected" : "Disconnected");
    }
}
