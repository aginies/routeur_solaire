#include "ConfigManager.h"
#include "Logger.h"
#include <Preferences.h>

const char* ConfigManager::CONFIG_FILE = "/config.json";

Config ConfigManager::load() {
    Config config;
    JsonDocument doc;
    bool loadedFromFile = false;

    // 1. Try loading from LittleFS
    if (LittleFS.exists(CONFIG_FILE)) {
        File file = LittleFS.open(CONFIG_FILE, "r");
        if (file) {
            DeserializationError error = deserializeJson(doc, file);
            file.close();
            if (!error) {
                loadedFromFile = true;
                Logger::info("ConfigManager: Loaded from LittleFS");
            } else {
                Logger::error("ConfigManager: LittleFS JSON error: " + String(error.c_str()));
            }
        }
    }

    // 2. Fallback to Preferences (NVS) ONLY if LittleFS failed
    if (!loadedFromFile) {
        Preferences prefs;
        prefs.begin("solar_config", true); // Read-only
        if (prefs.isKey("json")) {
            String nvsJson = prefs.getString("json");
            DeserializationError error = deserializeJson(doc, nvsJson);
            if (!error) {
                Logger::info("ConfigManager: Recovered from NVS backup");
                loadedFromFile = true;
            }
        }
        prefs.end();
    }

    if (!loadedFromFile) {
        Logger::info("ConfigManager: No config found, using defaults");
        save(config);
        return config;
    }

    // --- Apply merged values to config object ---
    // System
    if (doc.containsKey("name")) config.name = doc["name"].as<String>();
    if (doc.containsKey("timezone")) config.timezone = doc["timezone"].as<String>();
    if (doc.containsKey("cpu_freq")) config.cpu_freq = doc["cpu_freq"];
    if (doc.containsKey("internal_led_pin")) config.internal_led_pin = doc["internal_led_pin"];
    if (doc.containsKey("max_esp32_temp")) config.max_esp32_temp = doc["max_esp32_temp"];

    // WiFi
    if (doc.containsKey("e_wifi")) config.e_wifi = doc["e_wifi"];
    if (doc.containsKey("wifi_ssid")) config.wifi_ssid = doc["wifi_ssid"].as<String>();
    if (doc.containsKey("wifi_password")) config.wifi_password = doc["wifi_password"].as<String>();
    if (doc.containsKey("wifi_static_ip")) config.wifi_static_ip = doc["wifi_static_ip"].as<String>();
    if (doc.containsKey("wifi_subnet")) config.wifi_subnet = doc["wifi_subnet"].as<String>();
    if (doc.containsKey("wifi_gateway")) config.wifi_gateway = doc["wifi_gateway"].as<String>();
    if (doc.containsKey("wifi_dns")) config.wifi_dns = doc["wifi_dns"].as<String>();

    // AP
    if (doc.containsKey("ap_ssid")) config.ap_ssid = doc["ap_ssid"].as<String>();
    if (doc.containsKey("ap_password")) config.ap_password = doc["ap_password"].as<String>();
    if (doc.containsKey("ap_hidden_ssid")) config.ap_hidden_ssid = doc["ap_hidden_ssid"];
    if (doc.containsKey("ap_channel")) config.ap_channel = doc["ap_channel"];
    if (doc.containsKey("ap_ip")) config.ap_ip = doc["ap_ip"].as<String>();

    // Hardware
    if (doc.containsKey("ssr_pin")) config.ssr_pin = doc["ssr_pin"];
    if (doc.containsKey("relay_pin")) config.relay_pin = doc["relay_pin"];
    if (doc.containsKey("ds18b20_pin")) config.ds18b20_pin = doc["ds18b20_pin"];
    if (doc.containsKey("fan_pin")) config.fan_pin = doc["fan_pin"];
    if (doc.containsKey("zx_pin")) config.zx_pin = doc["zx_pin"];

    // Shelly
    if (doc.containsKey("shelly_em_ip")) config.shelly_em_ip = doc["shelly_em_ip"].as<String>();
    if (doc.containsKey("e_shelly_mqtt")) config.e_shelly_mqtt = doc["e_shelly_mqtt"];
    if (doc.containsKey("shelly_mqtt_topic")) config.shelly_mqtt_topic = doc["shelly_mqtt_topic"].as<String>();
    if (doc.containsKey("fake_shelly")) config.fake_shelly = doc["fake_shelly"];
    if (doc.containsKey("poll_interval")) config.poll_interval = doc["poll_interval"];

    // Equipment
    if (doc.containsKey("equipment_name")) config.equipment_name = doc["equipment_name"].as<String>();
    if (doc.containsKey("equipment_max_power")) config.equipment_max_power = doc["equipment_max_power"];
    if (doc.containsKey("export_setpoint")) config.export_setpoint = doc["export_setpoint"];

    // Equipment 2
    if (doc.containsKey("e_equip2")) config.e_equip2 = doc["e_equip2"];
    if (doc.containsKey("equip2_name")) config.equip2_name = doc["equip2_name"].as<String>();
    if (doc.containsKey("equip2_shelly_ip")) config.equip2_shelly_ip = doc["equip2_shelly_ip"].as<String>();
    if (doc.containsKey("equip2_max_power")) config.equip2_max_power = doc["equip2_max_power"];
    if (doc.containsKey("equip2_priority")) config.equip2_priority = doc["equip2_priority"];
    if (doc.containsKey("equip2_min_on_time")) config.equip2_min_on_time = doc["equip2_min_on_time"];
    if (doc.containsKey("equip2_schedule")) config.equip2_schedule = doc["equip2_schedule"];

    // Weather
    if (doc.containsKey("e_weather")) config.e_weather = doc["e_weather"];
    if (doc.containsKey("weather_lat")) config.weather_lat = doc["weather_lat"].as<String>();
    if (doc.containsKey("weather_lon")) config.weather_lon = doc["weather_lon"].as<String>();
    if (doc.containsKey("weather_cloud_threshold")) config.weather_cloud_threshold = doc["weather_cloud_threshold"];

    // Incremental Controller
    if (doc.containsKey("delta")) config.delta = doc["delta"];
    if (doc.containsKey("deltaneg")) config.deltaneg = doc["deltaneg"];
    if (doc.containsKey("compensation")) config.compensation = doc["compensation"];
    if (doc.containsKey("dynamic_threshold_w")) config.dynamic_threshold_w = doc["dynamic_threshold_w"];

    // Control
    if (doc.containsKey("max_duty_percent")) config.max_duty_percent = doc["max_duty_percent"];
    if (doc.containsKey("burst_period")) config.burst_period = doc["burst_period"];
    if (doc.containsKey("min_power_threshold")) config.min_power_threshold = doc["min_power_threshold"];
    if (doc.containsKey("min_off_time")) config.min_off_time = doc["min_off_time"];
    if (doc.containsKey("boost_minutes")) config.boost_minutes = doc["boost_minutes"];

    // Force/Night Mode
    if (doc.containsKey("force_equipment")) config.force_equipment = doc["force_equipment"];
    if (doc.containsKey("e_force_window")) config.e_force_window = doc["e_force_window"];
    if (doc.containsKey("force_start")) config.force_start = doc["force_start"].as<String>();
    if (doc.containsKey("force_end")) config.force_end = doc["force_end"].as<String>();
    if (doc.containsKey("night_start")) config.night_start = doc["night_start"].as<String>();
    if (doc.containsKey("night_end")) config.night_end = doc["night_end"].as<String>();
    if (doc.containsKey("night_poll_interval")) config.night_poll_interval = doc["night_poll_interval"];

    // Temperature
    if (doc.containsKey("e_ssr_temp")) config.e_ssr_temp = doc["e_ssr_temp"];
    if (doc.containsKey("ssr_max_temp")) config.ssr_max_temp = doc["ssr_max_temp"];

    // Fan
    if (doc.containsKey("e_fan")) config.e_fan = doc["e_fan"];
    if (doc.containsKey("fan_temp_offset")) config.fan_temp_offset = doc["fan_temp_offset"];

    // JSY
    if (doc.containsKey("e_jsy")) config.e_jsy = doc["e_jsy"];
    if (doc.containsKey("jsy_uart_id")) config.jsy_uart_id = doc["jsy_uart_id"];
    if (doc.containsKey("jsy_tx")) config.jsy_tx = doc["jsy_tx"];
    if (doc.containsKey("jsy_rx")) config.jsy_rx = doc["jsy_rx"];

    // SSR Control
    if (doc.containsKey("control_mode")) config.control_mode = doc["control_mode"].as<String>();
    if (doc.containsKey("half_period_us")) config.half_period_us = doc["half_period_us"];
    if (doc.containsKey("zx_busypoll_us")) config.zx_busypoll_us = doc["zx_busypoll_us"];
    if (doc.containsKey("zx_timeout_ms")) config.zx_timeout_ms = doc["zx_timeout_ms"];
    if (doc.containsKey("debug_phase")) config.debug_phase = doc["debug_phase"];

    // MQTT
    if (doc.containsKey("e_mqtt")) config.e_mqtt = doc["e_mqtt"];
    if (doc.containsKey("mqtt_ip")) config.mqtt_ip = doc["mqtt_ip"].as<String>();
    if (doc.containsKey("mqtt_port")) config.mqtt_port = doc["mqtt_port"];
    if (doc.containsKey("mqtt_user")) config.mqtt_user = doc["mqtt_user"].as<String>();
    if (doc.containsKey("mqtt_password")) config.mqtt_password = doc["mqtt_password"].as<String>();
    if (doc.containsKey("mqtt_name")) config.mqtt_name = doc["mqtt_name"].as<String>();
    if (doc.containsKey("mqtt_retain")) config.mqtt_retain = doc["mqtt_retain"];
    if (doc.containsKey("mqtt_keepalive")) config.mqtt_keepalive = doc["mqtt_keepalive"];
    if (doc.containsKey("mqtt_discovery_prefix")) config.mqtt_discovery_prefix = doc["mqtt_discovery_prefix"].as<String>();
    if (doc.containsKey("mqtt_report_interval")) config.mqtt_report_interval = doc["mqtt_report_interval"];

    // Web Security
    if (doc.containsKey("web_user")) config.web_user = doc["web_user"].as<String>();
    if (doc.containsKey("web_password")) config.web_password = doc["web_password"].as<String>();

    return config;
}

bool ConfigManager::save(const Config& config) {
    Logger::info("ConfigManager: Saving config to LittleFS and NVS...");
    
    JsonDocument doc;
    doc["name"] = config.name;
    doc["timezone"] = config.timezone;
    doc["cpu_freq"] = config.cpu_freq;
    doc["internal_led_pin"] = config.internal_led_pin;
    doc["max_esp32_temp"] = config.max_esp32_temp;
    doc["e_wifi"] = config.e_wifi;
    doc["wifi_ssid"] = config.wifi_ssid;
    doc["wifi_password"] = config.wifi_password;
    doc["wifi_static_ip"] = config.wifi_static_ip;
    doc["wifi_subnet"] = config.wifi_subnet;
    doc["wifi_gateway"] = config.wifi_gateway;
    doc["wifi_dns"] = config.wifi_dns;
    doc["ap_ssid"] = config.ap_ssid;
    doc["ap_password"] = config.ap_password;
    doc["ap_hidden_ssid"] = config.ap_hidden_ssid;
    doc["ap_channel"] = config.ap_channel;
    doc["ap_ip"] = config.ap_ip;
    doc["ssr_pin"] = config.ssr_pin;
    doc["relay_pin"] = config.relay_pin;
    doc["ds18b20_pin"] = config.ds18b20_pin;
    doc["fan_pin"] = config.fan_pin;
    doc["zx_pin"] = config.zx_pin;
    doc["shelly_em_ip"] = config.shelly_em_ip;
    doc["e_shelly_mqtt"] = config.e_shelly_mqtt;
    doc["shelly_mqtt_topic"] = config.shelly_mqtt_topic;
    doc["fake_shelly"] = config.fake_shelly;
    doc["poll_interval"] = config.poll_interval;
    doc["equipment_name"] = config.equipment_name;
    doc["equipment_max_power"] = config.equipment_max_power;
    doc["export_setpoint"] = config.export_setpoint;

    // Equipment 2
    doc["e_equip2"] = config.e_equip2;
    doc["equip2_name"] = config.equip2_name;
    doc["equip2_shelly_ip"] = config.equip2_shelly_ip;
    doc["equip2_max_power"] = config.equip2_max_power;
    doc["equip2_priority"] = config.equip2_priority;
    doc["equip2_min_on_time"] = config.equip2_min_on_time;
    doc["equip2_schedule"] = config.equip2_schedule;

    // Weather
    doc["e_weather"] = config.e_weather;
    doc["weather_lat"] = config.weather_lat;
    doc["weather_lon"] = config.weather_lon;
    doc["weather_cloud_threshold"] = config.weather_cloud_threshold;

    doc["delta"] = config.delta;
    doc["deltaneg"] = config.deltaneg;
    doc["compensation"] = config.compensation;
    doc["dynamic_threshold_w"] = config.dynamic_threshold_w;
    doc["max_duty_percent"] = config.max_duty_percent;
    doc["burst_period"] = config.burst_period;
    doc["min_power_threshold"] = config.min_power_threshold;
    doc["min_off_time"] = config.min_off_time;
    doc["boost_minutes"] = config.boost_minutes;
    doc["force_equipment"] = config.force_equipment;
    doc["e_force_window"] = config.e_force_window;
    doc["force_start"] = config.force_start;
    doc["force_end"] = config.force_end;
    doc["night_start"] = config.night_start;
    doc["night_end"] = config.night_end;
    doc["night_poll_interval"] = config.night_poll_interval;
    doc["e_ssr_temp"] = config.e_ssr_temp;
    doc["ssr_max_temp"] = config.ssr_max_temp;
    doc["e_fan"] = config.e_fan;
    doc["fan_temp_offset"] = config.fan_temp_offset;
    doc["e_jsy"] = config.e_jsy;
    doc["jsy_uart_id"] = config.jsy_uart_id;
    doc["jsy_tx"] = config.jsy_tx;
    doc["jsy_rx"] = config.jsy_rx;
    doc["control_mode"] = config.control_mode;
    doc["half_period_us"] = config.half_period_us;
    doc["zx_busypoll_us"] = config.zx_busypoll_us;
    doc["zx_timeout_ms"] = config.zx_timeout_ms;
    doc["debug_phase"] = config.debug_phase;
    doc["e_mqtt"] = config.e_mqtt;
    doc["mqtt_ip"] = config.mqtt_ip;
    doc["mqtt_port"] = config.mqtt_port;
    doc["mqtt_user"] = config.mqtt_user;
    doc["mqtt_password"] = config.mqtt_password;
    doc["mqtt_name"] = config.mqtt_name;
    doc["mqtt_retain"] = config.mqtt_retain;
    doc["mqtt_keepalive"] = config.mqtt_keepalive;
    doc["mqtt_discovery_prefix"] = config.mqtt_discovery_prefix;
    doc["mqtt_report_interval"] = config.mqtt_report_interval;
    doc["web_user"] = config.web_user;
    doc["web_password"] = config.web_password;

    // 1. Save to LittleFS
    File file = LittleFS.open(CONFIG_FILE, "w");
    if (file) {
        serializeJson(doc, file);
        file.close();
        Logger::info("ConfigManager: Saved to LittleFS");
    } else {
        Logger::error("ConfigManager: ERROR opening LittleFS file for writing!");
    }

    // 2. Save to NVS (Preferences) - Full JSON string
    Preferences prefs;
    prefs.begin("solar_config", false);
    String output;
    serializeJson(doc, output);
    prefs.putString("json", output);
    prefs.end();
    Logger::info("ConfigManager: Saved to NVS");

    return true;
}

void ConfigManager::reset() {
    LittleFS.remove(CONFIG_FILE);
    Preferences prefs;
    prefs.begin("solar_config", false);
    prefs.clear();
    prefs.end();
}

