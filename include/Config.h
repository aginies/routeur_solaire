#ifndef CONFIG_H
#define CONFIG_H

#ifndef NATIVE_TEST
#include <Arduino.h>
#include <IPAddress.h>
#else
#include <string>
#include <iostream>
#include <cstdio>
class String : public std::string {
public:
    String() : std::string("") {}
    String(const char* s) : std::string(s) {}
    String(const std::string& s) : std::string(s) {}
    String(int v) : std::string(std::to_string(v)) {}
    String(float v, int p = 2) {
        // Bug #6 (header audit): honour precision arg `p` (was previously ignored,
        // mismatching real Arduino String's printf-style "%.*f" behaviour and tripping
        // tests that compare textual float output).
        char buf[32];
        snprintf(buf, sizeof(buf), "%.*f", p, v);
        std::string::assign(buf);
    }
    String operator+(const String& other) const { return String(std::string(*this) + other); }
    String operator+(const char* other) const { return String(std::string(*this) + std::string(other)); }
    void trim() {}
    int toInt() const { return std::stoi(*this); }
    float toFloat() const { return std::stof(*this); }
    int indexOf(char c) const { return find(c); }
    int indexOf(String s) const { return find(s); }
    String substring(int start, int end = -1) const { 
        if (end == -1) return String(substr(start));
        return String(substr(start, end - start));
    }
    void replace(String a, String b) {
        size_t pos = 0;
        while ((pos = find(a, pos)) != std::string::npos) {
            std::string::replace(pos, a.length(), b);
            pos += b.length();
        }
    }
    void toLowerCase() { for(auto &c : *this) c = tolower(c); }
    const char* c_str() const { return std::string::c_str(); }
};
#endif

#define FIRMWARE_VERSION "0.3.0"

struct Config {
    // System
    String name = "Solaire";
    String timezone = "CET-1CEST,M3.5.0,M10.5.0/3";
    int cpu_freq = 240;
    int internal_led_pin = 48;
    float max_esp32_temp = 65.0;

    // WiFi
    bool e_wifi = true;
    String wifi_ssid = "";
    String wifi_password = "";
    
    // WiFi Static IP
    String wifi_static_ip = "";
    String wifi_subnet = "";
    String wifi_gateway = "";
    String wifi_dns = "";

    // Access Point
    String ap_ssid = "W_Solaire";
    String ap_password = "12345678";
    bool ap_hidden_ssid = false;
    int ap_channel = 6;
    String ap_ip = "192.168.66.1";

    // Hardware Pins
    int ssr_pin = 12;
    int relay_pin = 13;
    int ds18b20_pin = 14;
    int fan_pin = 5;
    int zx_pin = 15;

    // Shelly / Power Monitoring
    String shelly_em_ip = "192.168.1.60";
    int shelly_em_index = 0; // 0 or 1
    bool e_shelly_mqtt = true;
    String shelly_mqtt_topic = "shellies/homeassistant/emeter/0/power";
    bool fake_shelly = false;
    int poll_interval = 1;

    // Equipment 1 (Ballon)
    String equip1_name = "Ballon";
    float equip1_max_power = 2300.0;
    // Bug #9 (header audit): export_setpoint moved here from the top-level block —
    // it semantically belongs to Equipment 1's solar-routing setpoint, not a
    // global system setting. Field name and serialization key are unchanged.
    float export_setpoint = 0.0;
    bool e_equip1 = false;
    String equip1_shelly_ip = "";
    int equip1_shelly_index = 0;
    bool e_equip1_mqtt = false;
    String equip1_mqtt_topic = "";

    // Equipment 2 (PAC / Shelly 1PM)
    bool e_equip2 = false;
    String equip2_name = "Piscine";
    String equip2_shelly_ip = "";
    int equip2_shelly_index = 0;
    bool e_equip2_mqtt = false;
    String equip2_mqtt_topic = "";
    float equip2_max_power = 1900.0;
    int equip2_priority = 1; // 1 = Water Heater first, 2 = PAC first
    int equip2_min_on_time = 15; // Minutes
    uint64_t equip2_schedule = 0; // 48 bits for 30min slots

    // Weather (Open-Meteo)
    bool e_weather = false;
    String weather_lat = "";
    String weather_lon = "";
    int weather_cloud_threshold = 40; // Minimum solar confidence percentage
    int solar_panel_power = 0; // Max solar panel power (W), 0 = disabled
    int solar_panel_azimuth = 180; // Panel azimuth (0=N, 90=E, 180=S, 270=W)

    // Incremental Controller (pv-router algorithm)
    float delta = 50.0;          // upper threshold (W) — above this, importing too much
    float deltaneg = 0.0;        // lower threshold (W) — below this, exporting surplus
    float compensation = 100.0;  // proportional gain factor
    float dynamic_threshold_w = 200.0; // dynamic lag threshold

    // Control
    float max_duty_percent = 100.0;  // cap max power redirected (0-100%)
    float burst_period = 0.5;
    float min_power_threshold = 10.0;
    int min_off_time = 1;
    int boost_minutes = 60;
    
    // Force Mode
    bool force_equipment = false;
    bool e_force_window = false;
    String force_start = "22:05";
    String force_end = "05:55";

    // Night Mode
    String night_start = "22:00";
    String night_end = "05:50";
    int night_poll_interval = 15;

    // Temperature Monitoring
    bool e_ssr_temp = true;
    float ssr_max_temp = 65.0;

    // Fan
    bool e_fan = true;
    int fan_temp_offset = 10;

    // Watchdog
    int shelly_timeout = 2;
    int safety_timeout = 10;

    // JSY-MK-194
    bool e_jsy = false;
    int jsy_uart_id = 2;
    int jsy_tx = 17;
    int jsy_rx = 16;

    // SSR Mode
    String control_mode = "trame";
    int half_period_us = 9900;
    int zx_busypoll_us = 1000;
    int zx_timeout_ms = 500;
    bool debug_phase = false;

    // MQTT
    bool e_mqtt = true;
    String mqtt_ip = "192.168.1.100";
    int mqtt_port = 1883;
    String mqtt_user = "";
    String mqtt_password = "";
    String mqtt_name = "GuiboSolar";
    bool mqtt_retain = false;
    int mqtt_keepalive = 60;
    String mqtt_discovery_prefix = "homeassistant";
    int mqtt_report_interval = 10;

    // Web Security
    String web_user = "";
    String web_password = "";
};

#endif
