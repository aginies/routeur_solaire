#include "ConfigManager.h"
#include "Logger.h"
#include "PinCapabilities.h"
#include <Preferences.h>

const char* ConfigManager::CONFIG_FILE = "/config.json";
const char* ConfigManager::CONFIG_TMP_FILE = "/config.json.tmp";
SemaphoreHandle_t ConfigManager::_saveMutex = nullptr;

// Create the save mutex once at boot so that save() can be called from any task
// without a race. Replaces the previous lazy-init pattern.
void ConfigManager::init() {
    if (_saveMutex == nullptr) {
        _saveMutex = xSemaphoreCreateMutex();
    }
}

// ArduinoJson v7 deprecates JsonDocument::containsKey(); use !doc[key].isNull() instead.
static inline bool has(JsonDocument& doc, const char* key) {
    return !doc[key].isNull();
}

// Validation helpers — applied only to fields where an out-of-range value would
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

// Bounded JSON buffers to prevent OOM on corrupted config files.
// ~4 KB covers all current fields with comfortable headroom (~2 KB raw JSON).
static const size_t CONFIG_JSON_CAPACITY = 4096;

Config ConfigManager::load() {
    Config config;
    DynamicJsonDocument doc(CONFIG_JSON_CAPACITY);
    if (doc.capacity() == 0) {
        Logger::error("ConfigManager: insufficient memory for JSON");
        save(config);
        return config;
    }
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
                // Previously this error was silently swallowed; now log it so the user knows
                // why we reverted to defaults despite an NVS backup existing.
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
    // cpu_freq must be one of the three supported steps on ESP32-S3.
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
    // 2.4 GHz channels are 1..13 (14 in JP, ignored).
    if (has(doc, "ap_channel")) config.ap_channel = clampInt(doc["ap_channel"].as<int>(), 1, 13, config.ap_channel, "ap_channel");
    if (has(doc, "ap_ip")) config.ap_ip = doc["ap_ip"].as<String>();

    // Hardware — GPIO bounds-check critical pins.
    if (has(doc, "ssr_pin"))     config.ssr_pin     = validatePinRole(doc["ssr_pin"].as<int>(),     config.ssr_pin,     PinRole::SSR);
    if (has(doc, "relay_pin"))   config.relay_pin   = validatePinRole(doc["relay_pin"].as<int>(),   config.relay_pin,   PinRole::RELAY);
    if (has(doc, "ds18b20_pin")) config.ds18b20_pin = validatePinRole(doc["ds18b20_pin"].as<int>(), config.ds18b20_pin, PinRole::DS18B20);
    if (has(doc, "fan_pin"))     config.fan_pin     = validatePinRole(doc["fan_pin"].as<int>(),     config.fan_pin,     PinRole::FAN_PWM);
    if (has(doc, "zx_pin"))      config.zx_pin      = validatePinRole(doc["zx_pin"].as<int>(),      config.zx_pin,      PinRole::ZX_INPUT);
    if (has(doc, "lcd_sda_pin")) config.lcd_sda_pin = validatePinRole(doc["lcd_sda_pin"].as<int>(), config.lcd_sda_pin, PinRole::LCD_SDA);
    if (has(doc, "lcd_scl_pin")) config.lcd_scl_pin = validatePinRole(doc["lcd_scl_pin"].as<int>(), config.lcd_scl_pin, PinRole::LCD_SCL);
    if (has(doc, "lcd_i2c_addr")) {
        byte raw = doc["lcd_i2c_addr"].as<byte>();
        if (isI2cAddressValid(raw)) {
            config.lcd_i2c_addr = raw;
        } else {
            Logger::warn(String("ConfigManager: invalid lcd_i2c_addr (") + String(raw, HEX) + "), using default 0x27");
        }
    }
    if (has(doc, "lcd_cols")) config.lcd_cols = clampInt(doc["lcd_cols"].as<int>(), 8, 40, config.lcd_cols, "lcd_cols");
    if (has(doc, "lcd_rows")) config.lcd_rows = clampInt(doc["lcd_rows"].as<int>(), 1, 4, config.lcd_rows, "lcd_rows");

    // Shelly
    if (has(doc, "shelly_em_ip")) config.shelly_em_ip = doc["shelly_em_ip"].as<String>();
    if (has(doc, "shelly_em_index")) config.shelly_em_index = doc["shelly_em_index"];
    if (has(doc, "e_shelly_mqtt")) config.e_shelly_mqtt = doc["e_shelly_mqtt"];
    if (has(doc, "shelly_mqtt_topic")) config.shelly_mqtt_topic = doc["shelly_mqtt_topic"].as<String>();
    if (has(doc, "grid_measure_source")) {
        config.grid_measure_source = doc["grid_measure_source"].as<String>();
        if (config.grid_measure_source == "jsy") config.grid_measure_source = "jsy1";
    }
    if (has(doc, "fake_shelly")) config.fake_shelly = doc["fake_shelly"];
    if (has(doc, "poll_interval")) config.poll_interval = doc["poll_interval"];
    if (has(doc, "shelly_timeout")) config.shelly_timeout = doc["shelly_timeout"];
    if (has(doc, "safety_timeout")) config.safety_timeout = doc["safety_timeout"];

    // Equipment 1
    // When both `equip1_name` (current) and `equipment_name` (legacy alias) are
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
    if (has(doc, "equip1_measure_source")) {
        config.equip1_measure_source = doc["equip1_measure_source"].as<String>();
        if (config.equip1_measure_source == "jsy") config.equip1_measure_source = "jsy1";
    }

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

    // Incremental Controller — validate delta/deltaneg against internal minimum deadzone.
    if (has(doc, "delta") || has(doc, "deltaneg")) {
        int32_t d = has(doc, "delta") ? doc["delta"].as<int>() : config.delta;
        int32_t dn = has(doc, "deltaneg") ? doc["deltaneg"].as<int>() : config.deltaneg;

        // Enforce a minimum deadzone (≥100 W total) so the controller never sees
        // a collapsed zone. Also clamp signs: delta must be ≥ 0 and deltaneg ≤ 0.
        bool warned = false;
        if (dn > d) { Logger::warn("ConfigManager: deltaneg > delta, swapping"); warned = true; }

        // If both have wrong sign or the gap is too small, expand to ±50W.
        int32_t span = d - dn;  // always ≥ 0 after swap
        if (span < 100) {
            Logger::warn("ConfigManager: delta/deltaneg gap too small (<100 W), expanding to ±50W");
            d = 50;
            dn = -50;
            warned = false;
        }

        config.delta = d;
        config.deltaneg = dn;
    } else {
        // No delta/deltaneg in JSON at all — keep defaults.
    }
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
    if (has(doc, "jsy1_tx")) config.jsy1_tx = validatePinRole(doc["jsy1_tx"].as<int>(), config.jsy1_tx, PinRole::JSY1_TX);
    else if (has(doc, "jsy_tx")) config.jsy1_tx = validatePinRole(doc["jsy_tx"].as<int>(), config.jsy1_tx, PinRole::JSY1_TX);

    if (has(doc, "jsy1_rx")) config.jsy1_rx = validatePinRole(doc["jsy1_rx"].as<int>(), config.jsy1_rx, PinRole::JSY1_RX);
    else if (has(doc, "jsy_rx")) config.jsy1_rx = validatePinRole(doc["jsy_rx"].as<int>(), config.jsy1_rx, PinRole::JSY1_RX);

    if (has(doc, "jsy2_tx")) config.jsy2_tx = validatePinRole(doc["jsy2_tx"].as<int>(), config.jsy2_tx, PinRole::JSY2_TX);
    if (has(doc, "jsy2_rx")) config.jsy2_rx = validatePinRole(doc["jsy2_rx"].as<int>(), config.jsy2_rx, PinRole::JSY2_RX);

    if (has(doc, "jsy_grid_channel")) config.jsy_grid_channel = clampInt(doc["jsy_grid_channel"].as<int>(), 1, 2, config.jsy_grid_channel, "jsy_grid_channel");
    if (has(doc, "jsy_equip1_channel")) config.jsy_equip1_channel = clampInt(doc["jsy_equip1_channel"].as<int>(), 1, 2, config.jsy_equip1_channel, "jsy_equip1_channel");

    // SSR Control
    if (has(doc, "control_mode")) config.control_mode = doc["control_mode"].as<String>();
    if (has(doc, "half_period_us")) config.half_period_us = doc["half_period_us"];
    if (has(doc, "zx_busypoll_us")) config.zx_busypoll_us = doc["zx_busypoll_us"];
    if (has(doc, "zx_timeout_ms")) config.zx_timeout_ms = doc["zx_timeout_ms"];

    // Phase-angle calibration (valid only when control_mode == "phase")
    if (has(doc, "phase_calibrate")) config.phase_calibrate = doc["phase_calibrate"];
    if (has(doc, "phase_cal_min_us")) config.phase_cal_min_us = clampInt(doc["phase_cal_min_us"].as<int>(), 10, 9990, config.phase_cal_min_us, "phase_cal_min_us");
    if (has(doc, "phase_cal_max_us")) config.phase_cal_max_us = clampInt(doc["phase_cal_max_us"].as<int>(), 20, 10000, config.phase_cal_max_us, "phase_cal_max_us");
    if (has(doc, "phase_cal_step_us")) config.phase_cal_step_us = clampInt(doc["phase_cal_step_us"].as<int>(), 10, 5000, config.phase_cal_step_us, "phase_cal_step_us");
    if (has(doc, "phase_cal_hold_ms")) config.phase_cal_hold_ms = clampInt(doc["phase_cal_hold_ms"].as<int>(), 1000, 30000, config.phase_cal_hold_ms, "phase_cal_hold_ms");

    // MQTT
    if (has(doc, "e_mqtt")) config.e_mqtt = doc["e_mqtt"];
    if (has(doc, "mqtt_ip")) config.mqtt_ip = doc["mqtt_ip"].as<String>();
    // TCP port range is 1..65535.
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

    // Take the save mutex so concurrent web/MQTT triggers don't trample each other
    // mid-write. Wait up to 5 s; if we can't get it, refuse the save.
    if (xSemaphoreTake(_saveMutex, pdMS_TO_TICKS(5000)) != pdTRUE) {
        Logger::error("ConfigManager: save() could not acquire mutex (timeout)");
        return false;
    }

    // Bounded allocation — prevents OOM if config is unexpectedly large.
    DynamicJsonDocument doc(CONFIG_JSON_CAPACITY);

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
    doc["lcd_sda_pin"] = config.lcd_sda_pin;
    doc["lcd_scl_pin"] = config.lcd_scl_pin;
    doc["lcd_i2c_addr"] = config.lcd_i2c_addr;
    doc["lcd_cols"] = config.lcd_cols;
    doc["lcd_rows"] = config.lcd_rows;
    doc["shelly_em_ip"] = config.shelly_em_ip;
    doc["shelly_em_index"] = config.shelly_em_index;
    doc["e_shelly_mqtt"] = config.e_shelly_mqtt;
    doc["shelly_mqtt_topic"] = config.shelly_mqtt_topic;
    doc["grid_measure_source"] = config.grid_measure_source;
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
    doc["equip1_measure_source"] = config.equip1_measure_source;

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
    doc["jsy1_tx"] = config.jsy1_tx;
    doc["jsy1_rx"] = config.jsy1_rx;
    doc["jsy2_tx"] = config.jsy2_tx;
    doc["jsy2_rx"] = config.jsy2_rx;
    doc["jsy_grid_channel"] = config.jsy_grid_channel;
    doc["jsy_equip1_channel"] = config.jsy_equip1_channel;
    doc["control_mode"] = config.control_mode;
    doc["half_period_us"] = config.half_period_us;
    doc["zx_busypoll_us"] = config.zx_busypoll_us;
    doc["zx_timeout_ms"] = config.zx_timeout_ms;
    // Phase-angle calibration (persisted so config survives reboot)
    doc["phase_calibrate"] = config.phase_calibrate;
    doc["phase_cal_min_us"] = config.phase_cal_min_us;
    doc["phase_cal_max_us"] = config.phase_cal_max_us;
    doc["phase_cal_step_us"] = config.phase_cal_step_us;
    doc["phase_cal_hold_ms"] = config.phase_cal_hold_ms;
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

    // 1. Save to LittleFS — write to .tmp then rename atomically so a power loss
    // mid-write leaves the previous valid config intact. Also check serializeJson() return value.
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

    // 2. Save to NVS (Preferences) - Full JSON string; putString() returns bytes written, 0 == failure.
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
    // Return real status: both stores must succeed for save() to be considered OK.
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
