#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <ArduinoJson.h>
#include <Update.h>

#include "Config.h"
#include "JkBmsProtocol.h"
#include "JkBleClient.h"
#include "WebDashboard.h"

#if ENABLE_TAILSCALE
#include "microlink.h"
#include "microlink_internal.h"
#endif

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

#if ENABLE_TAILSCALE
static microlink_t* mlHandle = nullptr;
static microlink_state_t tsState = ML_STATE_IDLE;
static String tsVpnIpStr = "";
static String tsStatusStr = "IDLE";

static void onTailscaleStateChange(microlink_t* ml, microlink_state_t state, void* user_data) {
    tsState = state;
    const char *state_names[] = {
        "IDLE", "WIFI_WAIT", "CONNECTING", "REGISTERING",
        "CONNECTED", "RECONNECTING", "ERROR"
    };
    tsStatusStr = (state < sizeof(state_names)/sizeof(state_names[0])) ? state_names[state] : "UNKNOWN";
    Serial.printf("[TAILSCALE] State: %s\n", tsStatusStr.c_str());

    if (state == ML_STATE_CONNECTED) {
        uint32_t ip = microlink_get_vpn_ip(ml);
        char ip_str[16];
        microlink_ip_to_str(ip, ip_str);
        tsVpnIpStr = String(ip_str);
        Serial.printf("[TAILSCALE] Connected to Tailnet! VPN IP: %s\n", ip_str);
    }
}

void startTailscaleClient() {
    if (!currentConfig.ts_enabled || currentConfig.ts_auth_key.length() == 0) {
        Serial.println("[TAILSCALE] Disabled or no Auth Key configured.");
        return;
    }
    if (mlHandle != nullptr) {
        Serial.println("[TAILSCALE] Already running.");
        return;
    }

    Serial.printf("[TAILSCALE] Starting Tailscale client (hostname: %s)...\n", 
                  currentConfig.ts_hostname.c_str());

    microlink_config_t config = {
        .auth_key = currentConfig.ts_auth_key.c_str(),
        .device_name = currentConfig.ts_hostname.length() > 0 ? currentConfig.ts_hostname.c_str() : "jkbms-esp32",
        .enable_derp = true,
        .enable_stun = true,
        .enable_disco = true,
        .max_peers = 32,
        .wifi_tx_power_dbm = 13,
        .priority_peer_ip = 0,
        .disco_heartbeat_ms = 0,
        .stun_interval_ms = 0,
        .ctrl_watchdog_ms = 0
    };

    mlHandle = microlink_init(&config);
    if (!mlHandle) {
        Serial.println("[TAILSCALE] ERROR: Failed to initialize MicroLink!");
        tsStatusStr = "INIT_FAILED";
        return;
    }

    microlink_set_state_callback(mlHandle, onTailscaleStateChange, NULL);
    esp_err_t err = microlink_start(mlHandle);
    if (err != ESP_OK) {
        Serial.printf("[TAILSCALE] ERROR: microlink_start returned %d\n", err);
        tsStatusStr = "START_FAILED";
    } else {
        Serial.println("[TAILSCALE] MicroLink started in background.");
        tsStatusStr = "CONNECTING";
    }
}
#endif

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

#if ENABLE_TAILSCALE
        doc["ts_enabled"] = currentConfig.ts_enabled;
        doc["ts_connected"] = (tsState == ML_STATE_CONNECTED);
        doc["ts_ip"] = tsVpnIpStr;
        doc["ts_status"] = tsStatusStr;
        if (mlHandle) {
            doc["ts_derp_conn"] = mlHandle->derp.connected;
            doc["ts_peer_cnt"] = mlHandle->peer_count;
            doc["ts_derp_region"] = mlHandle->derp_home_region;
            JsonArray peers_arr = doc["ts_peers"].to<JsonArray>();
            for (int i = 0; i < mlHandle->peer_count && i < ML_MAX_PEERS; i++) {
                if (mlHandle->peers[i].active) {
                    char pip[16];
                    microlink_ip_to_str(mlHandle->peers[i].vpn_ip, pip);
                    JsonObject po = peers_arr.add<JsonObject>();
                    po["host"] = mlHandle->peers[i].hostname;
                    po["ip"] = pip;
                    po["direct"] = mlHandle->peers[i].has_direct_path;
                    po["wg_idx"] = mlHandle->peers[i].wg_peer_index;
                }
            }
        }
#else
        doc["ts_enabled"] = false;
        doc["ts_connected"] = false;
        doc["ts_ip"] = "";
        doc["ts_status"] = "NOT_SUPPORTED";
#endif

        doc["heap_free"] = ESP.getFreeHeap() / 1024;
        doc["psram_free"] = ESP.getFreePsram() / 1024;
        doc["psram_total"] = ESP.getPsramSize() / 1024;

        String out;
        serializeJson(doc, out);
        server.send(200, "application/json", out);
    });

    // API: Current Configuration
    server.on("/api/config", HTTP_GET, []() {
        JsonDocument doc;
        doc["ssid"] = currentConfig.wifi_ssid;
        doc["mac"]  = currentConfig.bms_mac;
        doc["pin"]  = currentConfig.bms_pin;
        doc["cells"] = currentConfig.cell_count;
        doc["ts_enabled"] = currentConfig.ts_enabled;
        doc["ts_hostname"] = currentConfig.ts_hostname;
        doc["has_ts_key"] = (currentConfig.ts_auth_key.length() > 0);
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

        if (doc["ts_enabled"].is<bool>()) {
            cfg.ts_enabled = doc["ts_enabled"].as<bool>();
        }
        if (doc["ts_hostname"].is<String>()) {
            String h = doc["ts_hostname"].as<String>();
            if (h.length() > 0) cfg.ts_hostname = h;
        }
        if (doc["ts_auth_key"].is<String>()) {
            String k = doc["ts_auth_key"].as<String>();
            if (k.length() > 0) {
                cfg.ts_auth_key = k;
            } else {
                cfg.ts_auth_key = currentConfig.ts_auth_key;
            }
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

    // Web OTA update endpoint (firmware flashing over network)
    server.on("/update", HTTP_POST, []() {
        server.sendHeader("Connection", "close");
        server.send(200, "text/plain", (Update.hasError()) ? "FAIL" : "OK");
        delay(500);
        ESP.restart();
    }, []() {
        HTTPUpload& upload = server.upload();
        if (upload.status == UPLOAD_FILE_START) {
            Serial.printf("[OTA] Update Start: %s\n", upload.filename.c_str());
            if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
                Update.printError(Serial);
            }
        } else if (upload.status == UPLOAD_FILE_WRITE) {
            if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
                Update.printError(Serial);
            }
        } else if (upload.status == UPLOAD_FILE_END) {
            if (Update.end(true)) {
                Serial.printf("[OTA] Success: %u bytes\n", upload.totalSize);
            } else {
                Update.printError(Serial);
            }
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

    Serial.printf("[SYSTEM] Chip: %s | CPU: %d MHz | Free Heap: %d KB | PSRAM: %d KB / %d KB\n",
                  ESP.getChipModel(),
                  ESP.getCpuFreqMHz(),
                  ESP.getFreeHeap() / 1024,
                  ESP.getFreePsram() / 1024,
                  ESP.getPsramSize() / 1024);

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
#if ENABLE_TAILSCALE
            startTailscaleClient();
#endif
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
#if ENABLE_TAILSCALE
        Serial.printf("[STATUS] IP: %s | Tailscale: %s (%s) | Free Heap: %d KB | PSRAM: %d KB | BLE: %s\n", 
                      WiFi.localIP().toString().c_str(), 
                      tsStatusStr.c_str(),
                      tsVpnIpStr.length() > 0 ? tsVpnIpStr.c_str() : "No IP",
                      ESP.getFreeHeap() / 1024,
                      ESP.getFreePsram() / 1024,
                      bleClient.isConnected() ? "Connected" : "Disconnected");
#else
        Serial.printf("[STATUS] IP: %s | Free Heap: %d KB | BLE: %s\n", 
                      WiFi.localIP().toString().c_str(), 
                      ESP.getFreeHeap() / 1024,
                      bleClient.isConnected() ? "Connected" : "Disconnected");
#endif
    }
}
