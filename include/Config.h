#pragma once

#include <Arduino.h>
#include <Preferences.h>
#include <nvs_flash.h>

#if ENABLE_TAILSCALE
#include "microlink.h"
#endif

struct AppConfig {
    String wifi_ssid = "";
    String wifi_pass = "";
    String bms_mac   = "";
    String bms_pin   = "1234";
    uint8_t cell_count = 4; // 4, 8, or 16
    bool ts_enabled  = false;
    String ts_auth_key = "";
    String ts_hostname = "jkbms-esp32";
};

class ConfigManager {
public:
    static AppConfig load() {
        esp_err_t err = nvs_flash_init();
        if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
            nvs_flash_erase();
            nvs_flash_init();
        }

        AppConfig cfg;
        Preferences prefs;
        if (prefs.begin("jkbms", true)) {
            cfg.wifi_ssid   = prefs.getString("ssid", "");
            cfg.wifi_pass   = prefs.getString("pass", "");
            cfg.bms_mac     = prefs.getString("mac", "");
            cfg.bms_pin     = prefs.getString("pin", "1234");
            cfg.cell_count  = prefs.getUChar("cells", 4);
            cfg.ts_enabled  = prefs.getBool("ts_en", false);
            cfg.ts_auth_key = prefs.getString("ts_key", "");
            cfg.ts_hostname = prefs.getString("ts_host", "jkbms-esp32");
            prefs.end();
        }
        if (cfg.cell_count != 4 && cfg.cell_count != 8 && cfg.cell_count != 16) {
            cfg.cell_count = 4;
        }
        if (cfg.ts_hostname.length() == 0) {
            cfg.ts_hostname = "jkbms-esp32";
        }
        return cfg;
    }

    static void save(const AppConfig& cfg) {
        Preferences prefs;
        prefs.begin("jkbms", false);
        prefs.putString("ssid", cfg.wifi_ssid);
        prefs.putString("pass", cfg.wifi_pass);
        prefs.putString("mac", cfg.bms_mac);
        prefs.putString("pin", cfg.bms_pin);
        prefs.putUChar("cells", cfg.cell_count);
        prefs.putBool("ts_en", cfg.ts_enabled);
        prefs.putString("ts_key", cfg.ts_auth_key);
        prefs.putString("ts_host", cfg.ts_hostname);
        prefs.end();
    }

    static void clear() {
        Preferences prefs;
        prefs.begin("jkbms", false);
        prefs.clear();
        prefs.end();
#if ENABLE_TAILSCALE
        microlink_factory_reset();
#endif
    }
};
