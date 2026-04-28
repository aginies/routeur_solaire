#include "WebManager.h"
#include "ConfigManager.h"
#include "Logger.h"
#include "NetworkManager.h"
#include "MqttManager.h"
#include "GridSensorService.h"
#include "StatsManager.h"
#include "ActuatorManager.h"
#include "TemperatureManager.h"
#include "SafetyManager.h"
#include "HistoryBuffer.h"
#include "WeatherManager.h"
#include "Equipment2Manager.h"
#include "Shelly1PMManager.h"
#include "Utils.h"
#include <LittleFS.h>
#include <Update.h>
#include <ArduinoJson.h>

AsyncWebServer WebManager::_server(80);
const Config* WebManager::_config = nullptr;
bool WebManager::_rebootRequested = false;

void WebManager::init(const Config& config) {
    _config = &config;
    setupRoutes();
    _server.begin();
    Logger::info("Web Server started");
}

void WebManager::loop() {
    if (_rebootRequested) {
        Logger::info("Reboot requested. Saving data...");
        Logger::flushAll(); // Save recent logs
        
#ifndef DISABLE_STATS
        StatsManager::save();   // Save daily stats
        HistoryBuffer::save(); // Save chart history
#endif

        delay(1000);
        Utils::reboot();
    }
}

String WebManager::templateProcessor(const String& var) {
    if (!_config) return String();

    // Logger::debug("Template request: " + var);
    if (var == "") return "%"; 
    if (var == "NAME") return _config->name;
    if (var == "EQUIPMENT_NAME") return _config->equipment_name;
    if (var == "VERSION") return String(FIRMWARE_VERSION);
    if (var == "BUILD_TIME") return String(__DATE__) + " " + String(__TIME__);

#ifdef MAX_STATS_DAYS
    if (var == "MAX_STATS_DAYS") return String(MAX_STATS_DAYS);
#else
    if (var == "MAX_STATS_DAYS") return "30";
#endif

    if (var == "STATS_LINK") {
#ifdef DISABLE_STATS
        return "";
#else
        return "<a href=\"/stats\" style=\"background:#f0c040; color:#1a1a2e; font-weight:bold;\">Statistiques</a>";
#endif
    }

    if (var == "EQUIP2_URL") return "/web_equip2";
    if (var == "EQUIP2_LINK") {
        if (_config->e_equip2) {
            return "<a href=\"/web_equip2\" style=\"background:#3498db; color:#fff; font-weight:bold;\">Planning Eq2</a>";
        }
        return "";
    }

    if (var == "TZ_PARIS") return (_config->timezone == "CET-1CEST,M3.5.0,M10.5.0/3") ? "selected" : "";
    if (var == "TZ_LONDON") return (_config->timezone == "GMT0BST,M3.5.0,M10.5.0/3")   ? "selected" : "";
    if (var == "TZ_ATHENS") return (_config->timezone == "EET-2EEST,M3.5.0,M10.5.0/3") ? "selected" : "";
    if (var == "TZ_UTC") return (_config->timezone == "UTC0")                      ? "selected" : "";
    
    if (var == "REBOOT_BANNER") return "";

    if (var == "EXTERNAL_WIFI_YES") return _config->e_wifi ? "selected" : "";
    if (var == "EXTERNAL_WIFI_NO") return !_config->e_wifi ? "selected" : "";
    if (var == "WIFI_SSID") return _config->wifi_ssid;
    if (var == "WIFI_PASSWORD") return _config->wifi_password;
    if (var == "WIFI_STATIC_IP") return _config->wifi_static_ip;
    if (var == "WIFI_SUBNET") return _config->wifi_subnet;
    if (var == "WIFI_GATEWAY") return _config->wifi_gateway;
    if (var == "WIFI_DNS") return _config->wifi_dns;

    if (var == "SHELLY_EM_IP") return _config->shelly_em_ip;
    if (var == "SHELLY_MQTT_YES") return _config->e_shelly_mqtt ? "selected" : "";
    if (var == "SHELLY_MQTT_NO") return !_config->e_shelly_mqtt ? "selected" : "";
    if (var == "SHELLY_MQTT_TOPIC") return _config->shelly_mqtt_topic;
    if (var == "POLL_INTERVAL") return String(_config->poll_interval);
    if (var == "SHELLY_TIMEOUT") return String(_config->shelly_timeout);
    if (var == "SAFETY_TIMEOUT") return String(_config->safety_timeout);

    if (var == "MQTT_YES") return _config->e_mqtt ? "selected" : "";
    if (var == "MQTT_NO") return !_config->e_mqtt ? "selected" : "";
    if (var == "MQTT_IP") return _config->mqtt_ip;
    if (var == "MQTT_PORT") return String(_config->mqtt_port);
    if (var == "MQTT_USER") return _config->mqtt_user;
    if (var == "MQTT_PASSWORD") return _config->mqtt_password;
    if (var == "MQTT_NAME") return _config->mqtt_name;

    if (var == "EQUIPMENT_MAX_POWER") return String(_config->equipment_max_power);
    if (var == "MAX_DUTY_PERCENT") return String(_config->max_duty_percent);
    if (var == "EXPORT_SETPOINT") return String(_config->export_setpoint);
    if (var == "DELTA") return String(_config->delta);
    if (var == "DELTANEG") return String(_config->deltaneg);
    if (var == "COMPENSATION") return String(_config->compensation);
    if (var == "DYNAMIC_THRESHOLD_W") return String(_config->dynamic_threshold_w);

    if (var == "DS18B20_PIN") return String(_config->ds18b20_pin);

    if (var == "FORCE_EQUIPMENT_YES") return _config->force_equipment ? "selected" : "";
    if (var == "FORCE_EQUIPMENT_NO") return !_config->force_equipment ? "selected" : "";
    if (var == "FORCE_WINDOW_YES") return _config->e_force_window ? "selected" : "";
    if (var == "FORCE_WINDOW_NO") return !_config->e_force_window ? "selected" : "";
    if (var == "FORCE_START") return _config->force_start;
    if (var == "FORCE_END") return _config->force_end;
    
    if (var == "NIGHT_START") return _config->night_start;
    if (var == "NIGHT_END") return _config->night_end;
    if (var == "NIGHT_POLL_INTERVAL") return String(_config->night_poll_interval);

    if (var == "JSY_YES") return _config->e_jsy ? "selected" : "";
    if (var == "JSY_NO") return !_config->e_jsy ? "selected" : "";
    if (var == "JSY_UART_ID") return String(_config->jsy_uart_id);
    if (var == "JSY_TX") return String(_config->jsy_tx);
    if (var == "JSY_RX") return String(_config->jsy_rx);
    if (var == "ZX_PIN") return String(_config->zx_pin);
    
    if (var == "MODE_BURST") return (_config->control_mode == "burst") ? "selected" : "";
    if (var == "MODE_CYCLE") return (_config->control_mode == "cycle_stealing") ? "selected" : "";
    if (var == "MODE_TRAME") return (_config->control_mode == "trame") ? "selected" : "";
    if (var == "MODE_PHASE") return (_config->control_mode == "phase") ? "selected" : "";

    if (var == "FAN_YES") return _config->e_fan ? "selected" : "";
    if (var == "FAN_NO") return !_config->e_fan ? "selected" : "";
    if (var == "FAN_PIN") return String(_config->fan_pin);
    if (var == "FAN_TEMP_OFFSET") return String(_config->fan_temp_offset);
    
    if (var == "SSR_TEMP_YES") return _config->e_ssr_temp ? "selected" : "";
    if (var == "SSR_TEMP_NO") return !_config->e_ssr_temp ? "selected" : "";
    if (var == "SSR_MAX_TEMP") return String(_config->ssr_max_temp);
    
    if (var == "I_LED_PIN") return String(_config->internal_led_pin);
    if (var == "SSR_PIN") return String(_config->ssr_pin);
    if (var == "RELAY_PIN") return String(_config->relay_pin);

    if (var == "SELECTED_20") return _config->cpu_freq == 20 ? "selected" : "";
    if (var == "SELECTED_40") return _config->cpu_freq == 40 ? "selected" : "";
    if (var == "SELECTED_80") return _config->cpu_freq == 80 ? "selected" : "";
    if (var == "SELECTED_160") return _config->cpu_freq == 160 ? "selected" : "";
    if (var == "SELECTED_240") return _config->cpu_freq == 240 ? "selected" : "";

    if (var == "WEATHER_YES") return _config->e_weather ? "selected" : "";
    if (var == "WEATHER_NO") return !_config->e_weather ? "selected" : "";
    if (var == "WEATHER_LAT") return _config->weather_lat;
    if (var == "WEATHER_LON") return _config->weather_lon;
    if (var == "WEATHER_THRESH") return String(_config->weather_cloud_threshold);

    if (var == "FAKE_SHELLY_YES") return _config->fake_shelly ? "selected" : "";
    if (var == "FAKE_SHELLY_NO") return !_config->fake_shelly ? "selected" : "";

    if (var == "WEB_USER") return _config->web_user;
    if (var == "WEB_PASSWORD") return _config->web_password;

    // Equipment 2 tokens
    if (var == "EQUIP2_NAME") return _config->equip2_name;
    if (var == "EQUIP2_IP") return _config->equip2_shelly_ip;
    if (var == "EQUIP2_POWER") return String(_config->equip2_max_power);
    if (var == "EQUIP2_MIN_TIME") return String(_config->equip2_min_on_time);
    if (var == "EQUIP2_ENABLED_YES") return _config->e_equip2 ? "selected" : "";
    if (var == "EQUIP2_ENABLED_NO") return !_config->e_equip2 ? "selected" : "";
    if (var == "EQUIP2_PRIO_1") return _config->equip2_priority == 1 ? "selected" : "";
    if (var == "EQUIP2_PRIO_2") return _config->equip2_priority == 2 ? "selected" : "";
    if (var == "E_EQUIP2_BOOL") return _config->e_equip2 ? "true" : "false";
    if (var == "EQUIP2_SCHEDULE_RAW") return String(_config->equip2_schedule);

#ifdef DISABLE_STATS
    if (var == "STATS_DISABLED_STYLE") return "display:none;";
#else
    if (var == "STATS_DISABLED_STYLE") return "";
#endif

#ifdef DISABLE_HISTORY
    if (var == "HISTORY_DISABLED_STYLE") return "display:none;";
#else
    if (var == "HISTORY_DISABLED_STYLE") return "";
#endif

#ifdef DISABLE_DATA_LOG
    if (var == "DATA_LOG_DISABLED_STYLE") return "display:none;";
#else
    if (var == "DATA_LOG_DISABLED_STYLE") return "";
#endif

    return String();
}

void WebManager::setupRoutes() {
    auto authRequired = [](AsyncWebServerRequest *request) -> bool {
        if (_config->web_user.length() > 0) {
            if (!request->authenticate(_config->web_user.c_str(), _config->web_password.c_str())) {
                request->requestAuthentication();
                return false;
            }
        }
        return true;
    };

    _server.on("/test", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(200, "text/plain", "Web server OK - Free heap: " + String(ESP.getFreeHeap()));
    });

    _server.on("/", HTTP_GET, [authRequired](AsyncWebServerRequest *request) {
        if (!authRequired(request)) return;
        request->send(LittleFS, "/web_command.html", "text/html", false, templateProcessor);
    });

    _server.on("/download_logs", HTTP_GET, [authRequired](AsyncWebServerRequest *request) {
        if (!authRequired(request)) return;
        
        // Prevent concurrent write/rotate while sending
        if (Logger::getMutex() && xSemaphoreTakeRecursive(Logger::getMutex(), pdMS_TO_TICKS(2000)) == pdTRUE) {
            Logger::flushAll(); // Ensure all buffered entries are on disk
            if (LittleFS.exists("/log.txt")) {
                AsyncWebServerResponse *response = request->beginResponse(LittleFS, "/log.txt", "text/plain");
                response->addHeader("Content-Disposition", "attachment; filename=solar_log.txt");
                request->send(response);
            } else {
                request->send(404, "text/plain", "Log file not found");
            }
            xSemaphoreGiveRecursive(Logger::getMutex());
        } else {
            request->send(503, "text/plain", "System busy, try again later");
        }
    });

    _server.on("/download_data", HTTP_GET, [authRequired](AsyncWebServerRequest *request) {
        if (!authRequired(request)) return;

        if (Logger::getMutex() && xSemaphoreTakeRecursive(Logger::getMutex(), pdMS_TO_TICKS(2000)) == pdTRUE) {
            Logger::flushAll();
            if (LittleFS.exists("/solar_data.txt")) {
                AsyncWebServerResponse *response = request->beginResponse(LittleFS, "/solar_data.txt", "text/plain");
                response->addHeader("Content-Disposition", "attachment; filename=solar_data.txt");
                request->send(response);
            } else {
                request->send(404, "text/plain", "Data file not found");
            }
            xSemaphoreGiveRecursive(Logger::getMutex());
        } else {
            request->send(503, "text/plain", "System busy, try again later");
        }
    });

    _server.on("/get_log_action", HTTP_GET, [](AsyncWebServerRequest *request) {
        Logger::streamLogs(request);
    });

    _server.on("/get_solar_data", HTTP_GET, [](AsyncWebServerRequest *request) {
        Logger::streamDataLogs(request);
    });

    _server.serveStatic("/chart.min.js", LittleFS, "/chart.min.js");
    _server.serveStatic("/icons", LittleFS, "/icons");

#ifndef DISABLE_STATS
    _server.serveStatic("/web_stats.html", LittleFS, "/web_stats.html");
    _server.on("/stats", HTTP_GET, [authRequired](AsyncWebServerRequest *request) {
        if (!authRequired(request)) return;
        request->send(LittleFS, "/web_stats.html", "text/html", false, templateProcessor);
    });
    _server.on("/get_stats", HTTP_GET, [authRequired](AsyncWebServerRequest *request) {
        if (!authRequired(request)) return;
        StatsManager::streamStatsJson(request);
    });

    static File uploadFile;
    _server.on("/import_stats", HTTP_POST, [authRequired](AsyncWebServerRequest *request) {
        if (!authRequired(request)) return;
        request->send(200, "text/plain", "Importation réussie. Redémarrage...");
        _rebootRequested = true;
    }, [authRequired](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
        if (!authRequired(request)) return;
        static size_t uploadedBytes = 0;
        static constexpr size_t MAX_STATS_UPLOAD_BYTES = 200 * 1024; // 200 KB hard limit
        if (!index) {
            uploadedBytes = 0;
            Logger::info("Importation des stats : " + filename);
            uploadFile = LittleFS.open("/stats.json", "w");
        }
        uploadedBytes += len;
        if (uploadedBytes > MAX_STATS_UPLOAD_BYTES) {
            if (uploadFile) { uploadFile.close(); }
            LittleFS.remove("/stats.json");
            Logger::error("Stats upload rejected: file too large (" + String(uploadedBytes) + " bytes)");
            return;
        }
        if (uploadFile) {
            if (len) uploadFile.write(data, len);
            if (final) uploadFile.close();
        }
    });
#endif

    _server.on("/web_config", HTTP_GET, [authRequired](AsyncWebServerRequest *request) {
        if (!authRequired(request)) return;
        request->send(LittleFS, "/web_config.html", "text/html", false, templateProcessor);
    });

    _server.on("/web_equip2", HTTP_GET, [authRequired](AsyncWebServerRequest *request) {
        if (!authRequired(request)) return;
        request->send(LittleFS, "/web_equip2.html", "text/html", false, templateProcessor);
    });

    _server.on("/save_config_eq2", HTTP_POST, [authRequired](AsyncWebServerRequest *request) {
        if (!authRequired(request)) return;
        
        auto getParam = [&](const char* name) -> String {
            return request->hasParam(name, true) ? request->getParam(name, true)->value() : String();
        };

        Config newCfg = *_config;
        newCfg.e_equip2 = (getParam("E_EQUIP2") == "True");
        newCfg.equip2_name = getParam("EQUIP2_NAME");
        newCfg.equip2_shelly_ip = getParam("EQUIP2_IP");
        newCfg.equip2_max_power = getParam("EQUIP2_POWER").toFloat();
        newCfg.equip2_min_on_time = getParam("EQUIP2_MIN_TIME").toInt();
        newCfg.equip2_priority = getParam("EQUIP2_PRIO").toInt();
        
        if (ConfigManager::save(newCfg)) {
            request->send(200, "text/plain", "OK");
            _rebootRequested = true;
        } else request->send(500);
    });
    _server.on("/save_eq2_schedule", HTTP_POST, [authRequired](AsyncWebServerRequest *request) {
        if (!authRequired(request)) return;
        Config newCfg = *_config;
        if (request->hasParam("schedule", true)) {
            String schedStr = request->getParam("schedule", true)->value();
            if (schedStr.length() == 0) schedStr = "0";
            newCfg.equip2_schedule = strtoull(schedStr.c_str(), NULL, 10);
            if (ConfigManager::save(newCfg)) request->send(200);
            else request->send(500);
        } else {
            request->send(400, "text/plain", "Missing schedule parameter");
        }
    });

    _server.on("/status_eq2", HTTP_GET, [](AsyncWebServerRequest *request) {
        JsonDocument doc;
        Eq2State s = Equipment2Manager::getState();
        String stateStr = "OFF";
        if (s == Eq2State::ON) stateStr = "MARCHE";
        else if (s == Eq2State::PENDING_OFF) stateStr = "ARRÊT IMMINENT";
        doc["state"] = stateStr;
        doc["power"] = Shelly1PMManager::getPower();
        doc["min_time"] = Equipment2Manager::getRemainingMinTime();
        String out;
        serializeJson(doc, out);
        request->send(200, "application/json", out);
    });


    _server.on("/save_config", HTTP_POST, [authRequired](AsyncWebServerRequest *request) {
        if (!authRequired(request)) return;
        
        auto getParam = [&](const char* name) -> String {
            return request->hasParam(name, true) ? request->getParam(name, true)->value() : String();
        };

        Config newCfg = *_config;
        newCfg.name = getParam("NAME");
        newCfg.equipment_name = getParam("EQUIPMENT_NAME");
        newCfg.timezone = getParam("TIMEZONE");
        newCfg.cpu_freq = getParam("CPU_FREQ").toInt();
        newCfg.wifi_ssid = getParam("WIFI_SSID");
        newCfg.wifi_password = getParam("WIFI_PASSWORD");
        newCfg.wifi_static_ip = getParam("WIFI_STATIC_IP");
        newCfg.wifi_subnet = getParam("WIFI_SUBNET");
        newCfg.wifi_gateway = getParam("WIFI_GATEWAY");
        newCfg.wifi_dns = getParam("WIFI_DNS");
        newCfg.e_wifi = (getParam("E_WIFI") == "True");
        newCfg.e_equip2 = (getParam("E_EQUIP2") == "True");
        newCfg.equip2_name = getParam("EQUIP2_NAME");
        newCfg.equip2_shelly_ip = getParam("EQUIP2_IP");
        newCfg.equip2_max_power = getParam("EQUIP2_POWER").toFloat();
        newCfg.equip2_priority = getParam("EQUIP2_PRIO").toInt();
        newCfg.equip2_min_on_time = getParam("EQUIP2_MIN_TIME").toInt();
        
        newCfg.shelly_em_ip = getParam("SHELLY_EM_IP");
        newCfg.e_shelly_mqtt = (getParam("E_SHELLY_MQTT") == "True");
        newCfg.shelly_mqtt_topic = getParam("SHELLY_MQTT_TOPIC");
        newCfg.poll_interval = getParam("POLL_INTERVAL").toInt();
        newCfg.shelly_timeout = getParam("SHELLY_TIMEOUT").toInt();
        newCfg.safety_timeout = getParam("SAFETY_TIMEOUT").toInt();
        
        newCfg.mqtt_ip = getParam("MQTT_IP");
        newCfg.mqtt_port = getParam("MQTT_PORT").toInt();
        newCfg.mqtt_user = getParam("MQTT_USER");
        newCfg.mqtt_password = getParam("MQTT_PASSWORD");
        newCfg.mqtt_name = getParam("MQTT_NAME");
        newCfg.e_mqtt = (getParam("E_MQTT") == "True");

        newCfg.equipment_max_power = getParam("EQUIPMENT_MAX_POWER").toFloat();
        newCfg.max_duty_percent = getParam("MAX_DUTY_PERCENT").toFloat();
        newCfg.export_setpoint = getParam("EXPORT_SETPOINT").toFloat();
        newCfg.delta = getParam("DELTA").toFloat();
        newCfg.deltaneg = getParam("DELTANEG").toFloat();
        newCfg.compensation = getParam("COMPENSATION").toFloat();
        newCfg.dynamic_threshold_w = getParam("DYNAMIC_THRESHOLD_W").toFloat();

        newCfg.ds18b20_pin = getParam("DS18B20_PIN").toInt();
        newCfg.ssr_pin = getParam("SSR_PIN").toInt();
        newCfg.relay_pin = getParam("RELAY_PIN").toInt();
        newCfg.internal_led_pin = getParam("I_LED_PIN").toInt();
        newCfg.fan_pin = getParam("FAN_PIN").toInt();
        newCfg.fan_temp_offset = getParam("FAN_TEMP_OFFSET").toInt();
        newCfg.e_fan = (getParam("E_FAN") == "True");
        newCfg.e_ssr_temp = (getParam("E_SSR_TEMP") == "True");
        newCfg.ssr_max_temp = getParam("SSR_MAX_TEMP").toFloat();

        newCfg.force_equipment = (getParam("FORCE_EQUIPMENT") == "True");
        newCfg.e_force_window = (getParam("E_FORCE_WINDOW") == "True");
        newCfg.force_start = getParam("FORCE_START");
        newCfg.force_end = getParam("FORCE_END");
        
        newCfg.night_start = getParam("NIGHT_START");
        newCfg.night_end = getParam("NIGHT_END");
        newCfg.night_poll_interval = getParam("NIGHT_POLL_INTERVAL").toInt();

        newCfg.e_jsy = (getParam("E_JSY") == "True");
        newCfg.jsy_uart_id = getParam("JSY_UART_ID").toInt();
        newCfg.jsy_tx = getParam("JSY_TX").toInt();
        newCfg.jsy_rx = getParam("JSY_RX").toInt();
        newCfg.zx_pin = getParam("ZX_PIN").toInt();
        newCfg.control_mode = getParam("CONTROL_MODE");

        newCfg.fake_shelly = (getParam("FAKE_SHELLY") == "True");

        newCfg.e_weather = (getParam("E_WEATHER") == "True");
        newCfg.weather_lat = getParam("WEATHER_LAT");
        newCfg.weather_lon = getParam("WEATHER_LON");
        newCfg.weather_cloud_threshold = getParam("WEATHER_THRESH").toInt();

        newCfg.web_user = getParam("WEB_USER");
        newCfg.web_password = getParam("WEB_PASSWORD");

        if (ConfigManager::save(newCfg)) {
            request->send(200, "text/plain", "Configuration sauvegardée. Redémarrage...");
            _rebootRequested = true;
        } else {
            request->send(500, "text/plain", "Erreur lors de la sauvegarde");
        }
    });

    _server.on("/update", HTTP_POST, [](AsyncWebServerRequest *request) {
        bool shouldReboot = !Update.hasError();
        AsyncWebServerResponse *response = request->beginResponse(200, "text/plain", shouldReboot ? "OK" : "FAIL");
        response->addHeader("Connection", "close");
        request->send(response);
        if (shouldReboot) _rebootRequested = true;
    }, [](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
        if (!index) {
            int command = (filename.indexOf("spiffs") > -1 || filename.indexOf("littlefs") > -1) ? U_SPIFFS : U_FLASH;
            if (!Update.begin(UPDATE_SIZE_UNKNOWN, command)) {
                Logger::error("OTA begin failed: " + String(Update.errorString()));
                return;
            }
        }
        if (Update.isRunning() && len) Update.write(data, len);
        if (Update.isRunning() && final) Update.end(true);
    });

    _server.on("/RESET_device", HTTP_GET, [authRequired](AsyncWebServerRequest *request) {
        if (!authRequired(request)) return;
        request->send(200, "text/plain", "Redémarrage en cours...");
        _rebootRequested = true;
    });

    _server.on("/RESET_config", HTTP_GET, [authRequired](AsyncWebServerRequest *request) {
        if (!authRequired(request)) return;
        ConfigManager::reset();
        request->send(200, "text/plain", "Configuration effacée. Redémarrage en cours...");
        _rebootRequested = true;
    });

    _server.on("/test_fan", HTTP_POST, [authRequired](AsyncWebServerRequest *request) {
        if (!authRequired(request)) return;
        int speed = request->hasParam("speed") ? request->getParam("speed")->value().toInt() : 0;
        ActuatorManager::setFanSpeed(speed, true);
        request->send(200, "application/json", "{\"success\":true}");
    });

    _server.on("/boost", HTTP_POST, [authRequired](AsyncWebServerRequest *request) {
        if (!authRequired(request)) return;
        ActuatorManager::startBoost(_config->boost_minutes);
        Logger::info("Manual boost started for " + String(_config->boost_minutes) + " min");
        request->redirect("/");
    });

    _server.on("/cancel_boost", HTTP_POST, [authRequired](AsyncWebServerRequest *request) {
        if (!authRequired(request)) return;
        ActuatorManager::boostEndTime = 0;
        Logger::info("Manual boost cancelled");
        request->redirect("/");
    });

    _server.on("/status", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(200, "application/json", getStatusJson());
    });

    _server.on("/history", HTTP_GET, [](AsyncWebServerRequest *request) {
        HistoryBuffer::streamHistoryJson(request);
    });
}

String WebManager::getStatusJson() {
    JsonDocument doc;
    doc["grid_power"] = GridSensorService::currentGridPower;
    doc["equipment_power"] = ActuatorManager::equipmentPower;
    doc["equip2_power"] = Shelly1PMManager::getPower();
    doc["boost_active"] = (SafetyManager::currentState == SystemState::STATE_BOOST);
    doc["force_mode"] = _config->force_equipment;
    doc["emergency_mode"] = (SafetyManager::currentState == SystemState::STATE_EMERGENCY_FAULT);
    doc["emergency_reason"] = SafetyManager::emergencyReason;
    doc["ssr_temp"] = (TemperatureManager::currentSsrTemp > -100.0) ? (float)TemperatureManager::currentSsrTemp : JsonVariant();
    doc["fan_active"] = ActuatorManager::fanActive;
    doc["fan_percent"] = ActuatorManager::fanPercent;
#ifndef DISABLE_STATS
    doc["total_import"] = StatsManager::totalImportToday;
    doc["total_redirect"] = StatsManager::totalRedirectToday;
    doc["total_export"] = StatsManager::totalExportToday;
#endif
    doc["free_ram"] = Utils::getFreeHeap();
    doc["total_ram"] = Utils::getTotalHeap();
    doc["free_psram"] = Utils::getFreePsram();
    doc["total_psram"] = Utils::getTotalPsram();
    doc["uptime"] = millis() / 1000;
    doc["rssi"] = NetworkManager::getRSSI();
    doc["version"] = String(FIRMWARE_VERSION);
    doc["build_time"] = String(__DATE__) + " " + String(__TIME__);

    doc["esp_temp"] = TemperatureManager::lastEspTemp;

    doc["grid_source"] = _config->e_shelly_mqtt ? "MQTT" : "HTTP";
    doc["shelly_link"] = (SafetyManager::currentState != SystemState::STATE_SAFE_TIMEOUT);
    doc["shelly_error"] = (SafetyManager::currentState == SystemState::STATE_SAFE_TIMEOUT);
    doc["shelly_mqtt_enabled"] = _config->e_shelly_mqtt;
    doc["mqtt_enabled"] = _config->e_mqtt;
    doc["mqtt_status"] = MqttManager::isConnected();
    doc["e_ssr_temp"] = _config->e_ssr_temp;
    doc["e_equip2"] = _config->e_equip2;
    doc["equip2_name"] = _config->equip2_name;
    doc["equip2_bypassed"] = Equipment2Manager::isBypassedByCloud();
    doc["night_mode"] = SolarMonitor::isNight(Utils::getCurrentMinutes());

    // Weather
    doc["e_weather"] = _config->e_weather;
    if (_config->e_weather) {
        doc["weather_clouds"] = WeatherManager::getCloudCover();
        doc["weather_clouds_low"] = WeatherManager::getCloudCoverLow();
        doc["weather_clouds_mid"] = WeatherManager::getCloudCoverMid();
        doc["weather_clouds_high"] = WeatherManager::getCloudCoverHigh();
        doc["weather_temp"] = WeatherManager::getTemperature();
        doc["weather_rain"] = WeatherManager::getRain();
        doc["weather_snow"] = WeatherManager::getSnow();
        doc["weather_age"] = millis() - WeatherManager::getLastUpdate();
        doc["weather_icon"] = WeatherManager::getWeatherIcon();
        doc["weather_too_cloudy"] = WeatherManager::isTooCloudy();
    }

    time_t now; time(&now); struct tm ti; localtime_r(&now, &ti);
    char buf[12]; strftime(buf, sizeof(buf), "%H:%M", &ti);
    doc["rtc_time"] = String(buf);
    char buf2[24]; strftime(buf2, sizeof(buf2), "%d/%m/%Y", &ti);
    doc["rtc_date"] = String(buf2);

    String output;
    serializeJson(doc, output);
    return output;
}

String WebManager::getHistoryJson() {
    return HistoryBuffer::getHistoryJson();
}
