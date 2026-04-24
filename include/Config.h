#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>
#include <IPAddress.h>

#define FIRMWARE_VERSION "0.2.0"

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
    bool e_shelly_mqtt = true;
    String shelly_mqtt_topic = "shellies/homeassistant/emeter/0/power";
    bool fake_shelly = false;
    int poll_interval = 1;

    // Equipment
    String equipment_name = "Ballon";
    float equipment_max_power = 2300.0;
    float export_setpoint = 0.0;

    // Incremental Controller (pv-router algorithm)
    float delta = 50.0;          // upper threshold (W) — above this, importing too much
    float deltaneg = 0.0;        // lower threshold (W) — below this, exporting surplus
    float compensation = 100.0;  // proportional gain factor

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
    int shelly_timeout = 10;

    // JSY-MK-194
    bool e_jsy = false;
    int jsy_uart_id = 2;
    int jsy_tx = 17;
    int jsy_rx = 16;

    // SSR Mode
    String control_mode = "burst";
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
