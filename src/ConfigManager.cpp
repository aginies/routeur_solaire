#include "ConfigManager.h"
#include "Logger.h"
#include "PinCapabilities.h"
#include <Preferences.h>

const char* ConfigManager::CONFIG_FILE = "/config.json";
const char* ConfigManager::CONFIG_TMP_FILE = "/config.json.tmp";
SemaphoreHandle_t ConfigManager::_saveMutex = nullptr;

void ConfigManager::ensureMutex() {
    if (_saveMutex == nullptr) {
        _saveMutex = xSemaphoreCreateMutex();
    }
}

// Bug #4: ArduinoJson v7 deprecates JsonDocument::containsKey(). The recommended
// replacement is `!doc[key].isNull()`. This helper centralises that so a future API change
// is a one-line edit.
static inline bool has(JsonDocument& doc, const char* key) {
    return !doc[key].isNull();
}

// Bug #5: validation helpers — applied only to fields where an out-of-range value would
// brick the device (wrong GPIO, invalid CPU freq, port overflow). Soft fields keep
// whatever the user wrote.
static int clampInt(int v, int lo, int hi, int dflt, const char* name) {
    if (v < lo || v > hi) {
        Logger::warn(String("ConfigManager: ") + name + " out of range (" + v + "), using default " + dflt);
        return dflt;
    }
    return v;
}

static int validatePinRole(int pin, int dflt, PinRole role) {
    if (pin == -1) return dflt;
    if (isPinValidForRole(pin, role)) return pin;
    Logger::warn(String("ConfigManager: invalid ") + pinRoleName(role) + " (" + pin + "): " + pinValidationReason(pin, role)
                 + ", using default " + dflt);
    return dflt;
}

static int validateCpuFreq(int f, int dflt) {
    // ESP32-S3 supports 80, 160, 240 MHz only.
    if (f == 80 || f == 160 || f == 240) return f;
    Logger::warn(String("ConfigManager: cpu_freq invalid (") + f + "), using default " + dflt);
    return dflt;
}

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
            } else {
                // Bug #6: previously this error was silently swallowed, leaving the user with
                // no clue why the system reverted to defaults despite an NVS backup existing.
                Logger::error("ConfigManager: NVS JSON error: " + String(error.c_str()));
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
    if (has(doc, "name")) config.name = doc["name"].as<String>();
    if (has(doc, "timezone")) config.timezone = doc["timezone"].as<String>();
    // Bug #5: cpu_freq must be one of the three supported steps on ESP32-S3.
    if (has(doc, "cpu_freq")) config.cpu_freq = validateCpuFreq(doc["cpu_freq"].as<int>(), config.cpu_freq);
    if (has(doc, "internal_led_pin")) config.internal_led_pin = validatePinRole(doc["internal_led_pin"].as<int>(), config.internal_led_pin, PinRole::INTERNAL_LED);
    if (has(doc, "max_esp32_temp")) config.max_esp32_temp = doc["max_esp32_temp"];

    // WiFi
    if (has(doc, "e_wifi")) config.e_wifi = doc["e_wifi"];
    if (has(doc, "wifi_ssid")) config.wifi_ssid = doc["wifi_ssid"].as<String>();
    if (has(doc, "wifi_password")) config.wifi_password = doc["wifi_password"].as<String>();
    if (has(doc, "wifi_static_ip")) config.wifi_static_ip = doc["wifi_static_ip"].as<String>();
    if (has(doc, "wifi_subnet")) config.wifi_subnet = doc["wifi_subnet"].as<String>();
    if (has(doc, "wifi_gateway")) config.wifi_gateway = doc["wifi_gateway"].as<String>();
    if (has(doc, "wifi_dns")) config.wifi_dns = doc["wifi_dns"].as<String>();

    // AP
    if (has(doc, "ap_ssid")) config.ap_ssid = doc["ap_ssid"].as<String>();
    if (has(doc, "ap_password")) config.ap_password = doc["ap_password"].as<String>();
    if (has(doc, "ap_hidden_ssid")) config.ap_hidden_ssid = doc["ap_hidden_ssid"];
    // Bug #5: 2.4 GHz channels are 1..13 (14 in JP, ignored).
    if (has(doc, "ap_channel")) config.ap_channel = clampInt(doc["ap_channel"].as<int>(), 1, 13, config.ap_channel, "ap_channel");
    if (has(doc, "ap_ip")) config.ap_ip = doc["ap_ip"].as<String>();

    // Hardware — Bug #5: GPIO bounds-check critical pins.
    if (has(doc, "ssr_pin"))     config.ssr_pin     = validatePinRole(doc["ssr_pin"].as<int>(),     config.ssr_pin,     PinRole::SSR);
    if (has(doc, "relay_pin"))   config.relay_pin   = validatePinRole(doc["relay_pin"].as<int>(),   config.relay_pin,   PinRole::RELAY);
    if (has(doc, "ds18b20_pin")) config.ds18b20_pin = validatePinRole(doc["ds18b20_pin"].as<int>(), config.ds18b20_pin, PinRole::DS18B20);
    if (has(doc, "fan_pin"))     config.fan_pin     = validatePinRole(doc["fan_pin"].as<int>(),     config.fan_pin,     PinRole::FAN_PWM);
    if (has(doc, "zx_pin"))      config.zx_pin      = validatePinRole(doc["zx_pin"].as<int>(),      config.zx_pin,      PinRole::ZX_INPUT);

    // Shelly
    if (has(doc, "shelly_em_ip")) config.shelly_em_ip = doc["shelly_em_ip"].as<String>();
    if (has(doc, "shelly_em_index")) config.shelly_em_index = doc["shelly_em_index"];
    if (has(doc, "e_shelly_mqtt")) config.e_shelly_mqtt = doc["e_shelly_mqtt"];
    if (has(doc, "shelly_mqtt_topic")) config.shelly_mqtt_topic = doc["shelly_mqtt_topic"].as<String>();
    if (has(doc, "fake_shelly")) config.fake_shelly = doc["fake_shelly"];
    if (has(doc, "poll_interval")) config.poll_interval = doc["poll_interval"];
    if (has(doc, "shelly_timeout")) config.shelly_timeout = doc["shelly_timeout"];
    if (has(doc, "safety_timeout")) config.safety_timeout = doc["safety_timeout"];

    // Equipment 1
    // Bug #9: when both `equip1_name` (current) and `equipment_name` (legacy alias) are
    // present in the JSON, the current key takes precedence. The else-if chain encodes that.
    if (has(doc, "equip1_name")) config.equip1_name = doc["equip1_name"].as<String>();
    else if (has(doc, "equipment_name")) config.equip1_name = doc["equipment_name"].as<String>();

    if (has(doc, "equip1_max_power")) config.equip1_max_power = doc["equip1_max_power"];
    else if (has(doc, "equipment_max_power")) config.equip1_max_power = doc["equipment_max_power"];
    
    if (has(doc, "export_setpoint")) config.export_setpoint = doc["export_setpoint"];
    if (has(doc, "e_equip1")) config.e_equip1 = doc["e_equip1"];
    if (has(doc, "equip1_shelly_ip")) config.equip1_shelly_ip = doc["equip1_shelly_ip"].as<String>();
    if (has(doc, "equip1_shelly_index")) config.equip1_shelly_index = doc["equip1_shelly_index"];
    if (has(doc, "e_equip1_mqtt")) config.e_equip1_mqtt = doc["e_equip1_mqtt"];
    if (has(doc, "equip1_mqtt_topic")) config.equip1_mqtt_topic = doc["equip1_mqtt_topic"].as<String>();

    // Equipment 2
    if (has(doc, "e_equip2")) config.e_equip2 = doc["e_equip2"];
    if (has(doc, "equip2_name")) config.equip2_name = doc["equip2_name"].as<String>();
    if (has(doc, "equip2_shelly_ip")) config.equip2_shelly_ip = doc["equip2_shelly_ip"].as<String>();
    if (has(doc, "equip2_shelly_index")) config.equip2_shelly_index = doc["equip2_shelly_index"];
    if (has(doc, "e_equip2_mqtt")) config.e_equip2_mqtt = doc["e_equip2_mqtt"];
    if (has(doc, "equip2_mqtt_topic")) config.equip2_mqtt_topic = doc["equip2_mqtt_topic"].as<String>();
    if (has(doc, "equip2_max_power")) config.equip2_max_power = doc["equip2_max_power"];
    if (has(doc, "equip2_priority")) config.equip2_priority = clampInt(doc["equip2_priority"].as<int>(), 1, 2, config.equip2_priority, "equip2_priority");
    if (has(doc, "equip2_min_on_time")) config.equip2_min_on_time = doc["equip2_min_on_time"];
    if (has(doc, "equip2_schedule")) config.equip2_schedule = doc["equip2_schedule"];

    // Weather
    if (has(doc, "e_weather")) config.e_weather = doc["e_weather"];
    if (has(doc, "weather_lat")) config.weather_lat = doc["weather_lat"].as<String>();
    if (has(doc, "weather_lon")) config.weather_lon = doc["weather_lon"].as<String>();
    if (has(doc, "weather_cloud_threshold")) config.weather_cloud_threshold = doc["weather_cloud_threshold"];
    if (has(doc, "solar_panel_power")) config.solar_panel_power = doc["solar_panel_power"];
    if (has(doc, "solar_panel_azimuth")) config.solar_panel_azimuth = clampInt(doc["solar_panel_azimuth"].as<int>(), 0, 359, config.solar_panel_azimuth, "solar_panel_azimuth");
    if (has(doc, "solar_panel_tilt")) config.solar_panel_tilt = clampInt(doc["solar_panel_tilt"].as<int>(), 0, 90, config.solar_panel_tilt, "solar_panel_tilt");
    if (has(doc, "solar_loss_factor")) config.solar_loss_factor = clampInt(doc["solar_loss_factor"].as<int>(), 0, 90, config.solar_loss_factor, "solar_loss_factor");

    // Incremental Controller
    if (has(doc, "delta")) config.delta = doc["delta"];
    if (has(doc, "deltaneg")) config.deltaneg = doc["deltaneg"];
    if (has(doc, "compensation")) config.compensation = doc["compensation"];
    if (has(doc, "dynamic_threshold_w")) config.dynamic_threshold_w = doc["dynamic_threshold_w"];

    // Control
    if (has(doc, "max_duty_percent")) config.max_duty_percent = doc["max_duty_percent"];
    if (has(doc, "burst_period")) config.burst_period = doc["burst_period"];
    if (has(doc, "min_power_threshold")) config.min_power_threshold = doc["min_power_threshold"];
    if (has(doc, "min_off_time")) config.min_off_time = doc["min_off_time"];
    if (has(doc, "boost_minutes")) config.boost_minutes = doc["boost_minutes"];
    if (has(doc, "vacation_until")) config.vacation_until = doc["vacation_until"];

    // Force/Night Mode
    if (has(doc, "force_equipment")) config.force_equipment = doc["force_equipment"];
    if (has(doc, "e_force_window")) config.e_force_window = doc["e_force_window"];
    if (has(doc, "force_start")) config.force_start = doc["force_start"].as<String>();
    if (has(doc, "force_end")) config.force_end = doc["force_end"].as<String>();
    if (has(doc, "night_start")) config.night_start = doc["night_start"].as<String>();
    if (has(doc, "night_end")) config.night_end = doc["night_end"].as<String>();
    if (has(doc, "night_poll_interval")) config.night_poll_interval = doc["night_poll_interval"];

    // Temperature
    if (has(doc, "e_ssr_temp")) config.e_ssr_temp = doc["e_ssr_temp"];
    if (has(doc, "ssr_max_temp")) config.ssr_max_temp = doc["ssr_max_temp"];

    // Fan
    if (has(doc, "e_fan")) config.e_fan = doc["e_fan"];
    if (has(doc, "fan_temp_offset")) config.fan_temp_offset = doc["fan_temp_offset"];

    // JSY
    if (has(doc, "e_jsy")) config.e_jsy = doc["e_jsy"];
    if (has(doc, "jsy_uart_id")) config.jsy_uart_id = clampInt(doc["jsy_uart_id"].as<int>(), 0, 2, config.jsy_uart_id, "jsy_uart_id");
    if (has(doc, "jsy_tx")) config.jsy_tx = validatePinRole(doc["jsy_tx"].as<int>(), config.jsy_tx, PinRole::JSY_TX);
    if (has(doc, "jsy_rx")) config.jsy_rx = validatePinRole(doc["jsy_rx"].as<int>(), config.jsy_rx, PinRole::JSY_RX);

    // SSR Control
    if (has(doc, "control_mode")) config.control_mode = doc["control_mode"].as<String>();
    if (has(doc, "half_period_us")) config.half_period_us = doc["half_period_us"];
    if (has(doc, "zx_busypoll_us")) config.zx_busypoll_us = doc["zx_busypoll_us"];
    if (has(doc, "zx_timeout_ms")) config.zx_timeout_ms = doc["zx_timeout_ms"];
    if (has(doc, "debug_phase")) config.debug_phase = doc["debug_phase"];

    // MQTT
    if (has(doc, "e_mqtt")) config.e_mqtt = doc["e_mqtt"];
    if (has(doc, "mqtt_ip")) config.mqtt_ip = doc["mqtt_ip"].as<String>();
    // Bug #5: TCP port range is 1..65535.
    if (has(doc, "mqtt_port")) config.mqtt_port = clampInt(doc["mqtt_port"].as<int>(), 1, 65535, config.mqtt_port, "mqtt_port");
    if (has(doc, "mqtt_user")) config.mqtt_user = doc["mqtt_user"].as<String>();
    if (has(doc, "mqtt_password")) config.mqtt_password = doc["mqtt_password"].as<String>();
    if (has(doc, "mqtt_name")) config.mqtt_name = doc["mqtt_name"].as<String>();
    if (has(doc, "mqtt_retain")) config.mqtt_retain = doc["mqtt_retain"];
    if (has(doc, "mqtt_keepalive")) config.mqtt_keepalive = doc["mqtt_keepalive"];
    if (has(doc, "mqtt_discovery_prefix")) config.mqtt_discovery_prefix = doc["mqtt_discovery_prefix"].as<String>();
    if (has(doc, "mqtt_report_interval")) config.mqtt_report_interval = doc["mqtt_report_interval"];

    // Web Security
    if (has(doc, "web_user")) config.web_user = doc["web_user"].as<String>();
    if (has(doc, "web_password")) config.web_password = doc["web_password"].as<String>();

    return config;
}

bool ConfigManager::save(const Config& config) {
    Logger::info("ConfigManager: Saving config to LittleFS and NVS...");

    // Bug #7: take the save mutex so concurrent web/MQTT triggers don't trample each other
    // mid-write. Wait up to 5 s; if we can't get it, refuse the save.
    ensureMutex();
    if (xSemaphoreTake(_saveMutex, pdMS_TO_TICKS(5000)) != pdTRUE) {
        Logger::error("ConfigManager: save() could not acquire mutex (timeout)");
        return false;
    }

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
    doc["shelly_em_index"] = config.shelly_em_index;
    doc["e_shelly_mqtt"] = config.e_shelly_mqtt;
    doc["shelly_mqtt_topic"] = config.shelly_mqtt_topic;
    doc["fake_shelly"] = config.fake_shelly;
    doc["poll_interval"] = config.poll_interval;
    doc["shelly_timeout"] = config.shelly_timeout;
    doc["safety_timeout"] = config.safety_timeout;
    doc["equip1_name"] = config.equip1_name;
    doc["equip1_max_power"] = config.equip1_max_power;
    doc["export_setpoint"] = config.export_setpoint;
    doc["e_equip1"] = config.e_equip1;
    doc["equip1_shelly_ip"] = config.equip1_shelly_ip;
    doc["equip1_shelly_index"] = config.equip1_shelly_index;
    doc["e_equip1_mqtt"] = config.e_equip1_mqtt;
    doc["equip1_mqtt_topic"] = config.equip1_mqtt_topic;

    // Equipment 2
    doc["e_equip2"] = config.e_equip2;
    doc["equip2_name"] = config.equip2_name;
    doc["equip2_shelly_ip"] = config.equip2_shelly_ip;
    doc["equip2_shelly_index"] = config.equip2_shelly_index;
    doc["e_equip2_mqtt"] = config.e_equip2_mqtt;
    doc["equip2_mqtt_topic"] = config.equip2_mqtt_topic;
    doc["equip2_max_power"] = config.equip2_max_power;
    doc["equip2_priority"] = config.equip2_priority;
    doc["equip2_min_on_time"] = config.equip2_min_on_time;
    doc["equip2_schedule"] = config.equip2_schedule;

    // Weather
    doc["e_weather"] = config.e_weather;
    doc["weather_lat"] = config.weather_lat;
    doc["weather_lon"] = config.weather_lon;
    doc["weather_cloud_threshold"] = config.weather_cloud_threshold;
    doc["solar_panel_power"] = config.solar_panel_power;
    doc["solar_panel_azimuth"] = config.solar_panel_azimuth;
    doc["solar_panel_tilt"] = config.solar_panel_tilt;
    doc["solar_loss_factor"] = config.solar_loss_factor;

    doc["delta"] = config.delta;
    doc["deltaneg"] = config.deltaneg;
    doc["compensation"] = config.compensation;
    doc["dynamic_threshold_w"] = config.dynamic_threshold_w;
    doc["max_duty_percent"] = config.max_duty_percent;
    doc["burst_period"] = config.burst_period;
    doc["min_power_threshold"] = config.min_power_threshold;
    doc["min_off_time"] = config.min_off_time;
    doc["boost_minutes"] = config.boost_minutes;
    doc["vacation_until"] = config.vacation_until;

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

    bool littleFsOk = false;
    bool nvsOk = false;

    // 1. Save to LittleFS — Bug #1: write to .tmp then rename atomically so a power loss
    // mid-write leaves the previous valid config intact.
    // Bug #2: check serializeJson() return value (bytes written).
    {
        // Pre-clean any leftover tmp from a previous crash.
        if (LittleFS.exists(CONFIG_TMP_FILE)) {
            LittleFS.remove(CONFIG_TMP_FILE);
        }
        File file = LittleFS.open(CONFIG_TMP_FILE, "w");
        if (!file) {
            Logger::error("ConfigManager: ERROR opening LittleFS tmp file for writing!");
        } else {
            size_t written = serializeJson(doc, file);
            file.close();
            if (written == 0) {
                Logger::error("ConfigManager: serializeJson wrote 0 bytes (flash full?)");
                LittleFS.remove(CONFIG_TMP_FILE);
            } else {
                // Remove old file then rename tmp into place. LittleFS::rename() does not
                // overwrite, so the explicit remove is required.
                if (LittleFS.exists(CONFIG_FILE)) LittleFS.remove(CONFIG_FILE);
                if (LittleFS.rename(CONFIG_TMP_FILE, CONFIG_FILE)) {
                    Logger::info("ConfigManager: Saved to LittleFS (atomic)");
                    littleFsOk = true;
                } else {
                    Logger::error("ConfigManager: rename of tmp config failed!");
                    LittleFS.remove(CONFIG_TMP_FILE);
                }
            }
        }
    }

    // 2. Save to NVS (Preferences) - Full JSON string
    // Bug #2: putString() returns the number of bytes written, 0 == failure.
    {
        Preferences prefs;
        if (!prefs.begin("solar_config", false)) {
            Logger::error("ConfigManager: NVS begin() failed");
        } else {
            String output;
            serializeJson(doc, output);
            size_t n = prefs.putString("json", output);
            prefs.end();
            if (n == 0) {
                Logger::error("ConfigManager: NVS putString returned 0 (NVS full?)");
            } else {
                Logger::info("ConfigManager: Saved to NVS");
                nvsOk = true;
            }
        }
    }

    xSemaphoreGive(_saveMutex);
    // Bug #3: return real status. Both stores must succeed for save() to be considered OK.
    return littleFsOk && nvsOk;
}

void ConfigManager::reset() {
    LittleFS.remove(CONFIG_FILE);
    LittleFS.remove(CONFIG_TMP_FILE);
    Preferences prefs;
    prefs.begin("solar_config", false);
    prefs.clear();
    prefs.end();
}
