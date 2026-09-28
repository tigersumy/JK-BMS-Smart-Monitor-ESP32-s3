#pragma once

#include <Arduino.h>
#include <Preferences.h>

struct AppConfig {
    String wifi_ssid = "";
    String wifi_pass = "";
    String bms_mac   = "";
    String bms_pin   = "1234";
    uint8_t cell_count = 4; // 4, 8, or 16
};

class ConfigManager {
public:
    static AppConfig load() {
        AppConfig cfg;
        Preferences prefs;
        prefs.begin("jkbms", true);
        cfg.wifi_ssid  = prefs.getString("ssid", "");
        cfg.wifi_pass  = prefs.getString("pass", "");
        cfg.bms_mac    = prefs.getString("mac", "");
        cfg.bms_pin    = prefs.getString("pin", "1234");
        cfg.cell_count = prefs.getUChar("cells", 4);
        if (cfg.cell_count != 4 && cfg.cell_count != 8 && cfg.cell_count != 16) {
            cfg.cell_count = 4;
        }
        prefs.end();
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
        prefs.end();
    }

    static void clear() {
        Preferences prefs;
        prefs.begin("jkbms", false);
        prefs.clear();
        prefs.end();
    }
};
