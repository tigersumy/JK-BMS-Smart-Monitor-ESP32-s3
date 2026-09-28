#pragma once

#include <Arduino.h>
#include <vector>

struct JkTelemetry {
    bool connected = false;
    uint32_t last_update = 0;
    
    // Cell voltages in Volts (supports 4S, 8S, 16S)
    uint8_t cell_count = 4;
    float cell_voltages[16] = {0.0f};
    float cell_resistances[16] = {0.0f};
    float min_cell_v = 0.0f;
    float max_cell_v = 0.0f;
    float delta_cell_v = 0.0f;
    uint8_t min_cell_idx = 0;
    uint8_t max_cell_idx = 0;

    // Pack totals
    float total_voltage = 0.0f; // V
    float current = 0.0f;       // A (signed: + charge, - discharge)
    float power = 0.0f;         // W
    float charge_power = 0.0f;  // W
    float discharge_power = 0.0f; // W
    float soc = 0.0f;           // %
    float capacity_remain = 0.0f; // Ah
    float cycle_capacity = 0.0f; // Ah
    uint32_t cycle_count = 0;

    // Temperatures
    float temp_mos = 0.0f;      // °C
    float temp_sensor1 = 0.0f;  // °C
    float temp_sensor2 = 0.0f;  // °C

    // Balancer
    bool balancing_active = false;
    float balancing_current = 0.0f; // A
    String balancer_direction = "Idle";

    // Switches
    bool switch_charging = false;
    bool switch_discharging = false;
    bool switch_balancer = false;

    // Errors & Status
    uint32_t raw_errors = 0;
    String errors_str = "OK";
};

// JK02 Holding registers for writing switches
static const uint8_t JK02_REG_CHARGE_SWITCH    = 29; // 0x1D
static const uint8_t JK02_REG_DISCHARGE_SWITCH = 30; // 0x1E
static const uint8_t JK02_REG_BALANCER_SWITCH  = 31; // 0x1F

// UUIDs for JK-BMS BLE
static const char* JK_SERVICE_UUID = "ffe0";
static const char* JK_CHAR_UUID    = "ffe1";
