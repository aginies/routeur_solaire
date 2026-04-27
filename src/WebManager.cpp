#include "WebManager.h"
#include "SolarMonitor.h"
#include "GridSensorService.h"
#include "NetworkManager.h"
#include "TemperatureManager.h"
#include "SafetyManager.h"
#include "ActuatorManager.h"
#include "HistoryBuffer.h"
#include "StatsManager.h"
#include "MqttManager.h"
#include "ConfigManager.h"
#include "Logger.h"
#include "Utils.h"
#include <WiFi.h>
#include <LittleFS.h>
#include <Update.h>

AsyncWebServer WebManager::_server(80);
AsyncWebSocket WebManager::_ws("/ws");
Config* WebManager::_config = nullptr;
bool WebManager::_initialized = false;
volatile bool WebManager::_rebootRequested = false;

void WebManager::init(Config& config) {
    _config = &config;
    setupWebSockets();
    setupRoutes();
    _server.begin();
    _initialized = true;
    Logger::info("Web Server started");
}

void WebManager::setupWebSockets() {
    _ws.onEvent([](AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len) {
        if (type == WS_EVT_CONNECT) {
            Logger::info("WS Client connected");
        } else if (type == WS_EVT_DISCONNECT) {
            Logger::info("WS Client disconnected");
        }
    });
    _server.addHandler(&_ws);
}

void WebManager::loop() {
    if (_initialized) {
        _ws.cleanupClients();
        Logger::loop();

        // Periodic broadcast (e.g., every 1s)
        static uint32_t lastBroadcast = 0;
        if (millis() - lastBroadcast >= 1000) {
            lastBroadcast = millis();
            if (_ws.count() > 0) {
                _ws.textAll(getStatusJson());
            }
        }
    }
    if (_rebootRequested) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        Utils::reboot();
    }
}

void WebManager::broadcastLog(const String& log) {
    if (_initialized) {
        _ws.cleanupClients();
        _ws.textAll(log);
    }
}

String WebManager::templateProcessor(const String& var) {
    if (!_config) return String();

    if (var == "") return "%"; 
    if (var == "NAME") return _config->name;
    if (var == "EQUIPMENT_NAME") return _config->equipment_name;
    if (var == "VERSION") return FIRMWARE_VERSION;
    if (var == "BUILD_TIME") return String(__DATE__) + " " + String(__TIME__);

#ifdef MAX_STATS_DAYS
    if (var == "MAX_STATS_DAYS") return String(MAX_STATS_DAYS);
#else
    if (var == "MAX_STATS_DAYS") return "30";
#endif

    if (var == "STATS_LINK") {
#ifdef DISABLE_STATS_PAGE
        return "";
#else
        return "<a href=\"/stats\" style=\"background:#f0c040; color:#1a1a2e; font-weight:bold;\">Statistiques</a>";
#endif
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

    if (var == "FAKE_SHELLY_YES") return _config->fake_shelly ? "selected" : "";
    if (var == "FAKE_SHELLY_NO") return !_config->fake_shelly ? "selected" : "";

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

    _server.on("/status", HTTP_GET, [authRequired](AsyncWebServerRequest *request) {
        if (!authRequired(request)) return;
        request->send(200, "application/json", getStatusJson());
    });

    _server.on("/history", HTTP_GET, [authRequired](AsyncWebServerRequest *request) {
        if (!authRequired(request)) return;
        HistoryBuffer::streamHistoryJson(request);
    });

    _server.on("/boost", HTTP_POST, [authRequired](AsyncWebServerRequest *request) {
        if (!authRequired(request)) return;
        int minutes = -1;
        if (request->hasParam("min")) minutes = request->getParam("min")->value().toInt();
        ActuatorManager::startBoost(minutes);
        request->redirect("/");
    });

    _server.on("/cancel_boost", HTTP_POST, [authRequired](AsyncWebServerRequest *request) {
        if (!authRequired(request)) return;
        ActuatorManager::cancelBoost();
        request->redirect("/");
    });

    _server.on("/test_fan", HTTP_POST, [authRequired](AsyncWebServerRequest *request) {
        if (!authRequired(request)) return;
        if (request->hasParam("speed")) {
            int speed = request->getParam("speed")->value().toInt();
            bool success = ActuatorManager::setFanSpeed(speed, true);
            request->send(200, "application/json", "{\"success\":" + String(success ? "true" : "false") + "}");
        } else {
            request->send(400, "application/json", "{\"success\":false}");
        }
    });

    _server.on("/RESET_device", HTTP_GET, [authRequired](AsyncWebServerRequest *request) {
        if (!authRequired(request)) return;
        request->send(200, "text/html", "<html><h1>Resetting...</h1></html>");
        _rebootRequested = true;
    });

    _server.on("/get_log_action", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(200, "text/plain", Logger::getLogs());
    });

    _server.on("/download_logs", HTTP_GET, [authRequired](AsyncWebServerRequest *request) {
        if (!authRequired(request)) return;
        AsyncWebServerResponse *response = request->beginResponse(LittleFS, "/log.txt", "text/plain", true);
        response->addHeader("Content-Disposition", "attachment; filename=solar_log.txt");
        request->send(response);
    });

    _server.on("/get_solar_data", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(200, "text/plain", Logger::getDataLogs());
    });

    _server.serveStatic("/chart.min.js", LittleFS, "/chart.min.js");

#ifndef DISABLE_STATS_PAGE
    _server.serveStatic("/web_stats.html", LittleFS, "/web_stats.html");
    _server.on("/stats", HTTP_GET, [authRequired](AsyncWebServerRequest *request) {
        if (!authRequired(request)) return;
        request->send(LittleFS, "/web_stats.html", "text/html", false, templateProcessor);
    });
    _server.on("/get_stats", HTTP_GET, [authRequired](AsyncWebServerRequest *request) {
        if (!authRequired(request)) return;
        StatsManager::streamStatsJson(request);
    });
#endif

    _server.on("/web_config", HTTP_GET, [authRequired](AsyncWebServerRequest *request) {
        if (!authRequired(request)) return;
        request->send(LittleFS, "/web_config.html", "text/html", false, templateProcessor);
    });

    _server.on("/save_config", HTTP_POST, [authRequired](AsyncWebServerRequest *request) {
        if (!authRequired(request)) return;
        // Config saving logic remains identical... 
        // (Truncated for brevity in this tool call, but implement fully in ActuatorManager update config)
        // Actually I will keep the original implementation logic here.
        
        auto getParam = [&](const char* name) -> String {
            return request->hasParam(name, true) ? request->getParam(name, true)->value() : String();
        };
        auto getBool = [&](const char* name) -> bool {
            String v = getParam(name); v.toLowerCase();
            return (v == "yes" || v == "1" || v == "true");
        };
        auto getInt = [&](const char* name, int def) -> int {
            String v = getParam(name); return v.length() > 0 ? v.toInt() : def;
        };
        auto getFloat = [&](const char* name, float def) -> float {
            String v = getParam(name); return v.length() > 0 ? v.toFloat() : def;
        };

        _config->name = getParam("NAME").length() > 0 ? getParam("NAME") : _config->name;
        _config->timezone = getParam("TIMEZONE").length() > 0 ? getParam("TIMEZONE") : _config->timezone;
        _config->cpu_freq = getInt("CPU_FREQ", _config->cpu_freq);
        _config->e_wifi = getBool("E_WIFI");
        _config->wifi_ssid = getParam("WIFI_SSID").length() > 0 ? getParam("WIFI_SSID") : _config->wifi_ssid;
        _config->wifi_password = getParam("WIFI_PASSWORD").length() > 0 ? getParam("WIFI_PASSWORD") : _config->wifi_password;
        _config->wifi_static_ip = getParam("WIFI_STATIC_IP");
        _config->wifi_subnet = getParam("WIFI_SUBNET");
        _config->wifi_gateway = getParam("WIFI_GATEWAY");
        _config->wifi_dns = getParam("WIFI_DNS");
        _config->shelly_em_ip = getParam("SHELLY_EM_IP").length() > 0 ? getParam("SHELLY_EM_IP") : _config->shelly_em_ip;
        _config->e_shelly_mqtt = getBool("E_SHELLY_MQTT");
        _config->shelly_mqtt_topic = getParam("SHELLY_MQTT_TOPIC").length() > 0 ? getParam("SHELLY_MQTT_TOPIC") : _config->shelly_mqtt_topic;
        _config->poll_interval = getInt("POLL_INTERVAL", _config->poll_interval);
        _config->fake_shelly = getBool("FAKE_SHELLY");
        _config->e_mqtt = getBool("E_MQTT");
        _config->mqtt_ip = getParam("MQTT_IP").length() > 0 ? getParam("MQTT_IP") : _config->mqtt_ip;
        _config->mqtt_port = getInt("MQTT_PORT", _config->mqtt_port);
        _config->mqtt_user = getParam("MQTT_USER");
        _config->mqtt_password = getParam("MQTT_PASSWORD");
        _config->mqtt_name = getParam("MQTT_NAME").length() > 0 ? getParam("MQTT_NAME") : _config->mqtt_name;
        _config->equipment_name = getParam("EQUIPMENT_NAME").length() > 0 ? getParam("EQUIPMENT_NAME") : _config->equipment_name;
        _config->equipment_max_power = getFloat("EQUIPMENT_MAX_POWER", _config->equipment_max_power);
        _config->max_duty_percent = getFloat("MAX_DUTY_PERCENT", _config->max_duty_percent);
        _config->export_setpoint = getFloat("EXPORT_SETPOINT", _config->export_setpoint);
        _config->delta = getFloat("DELTA", _config->delta);
        _config->deltaneg = getFloat("DELTANEG", _config->deltaneg);
        _config->compensation = getFloat("COMPENSATION", _config->compensation);
        _config->dynamic_threshold_w = getFloat("DYNAMIC_THRESHOLD_W", _config->dynamic_threshold_w);
        _config->ds18b20_pin = getInt("DS18B20_PIN", _config->ds18b20_pin);
        _config->e_ssr_temp = getBool("E_SSR_TEMP");
        _config->ssr_max_temp = getFloat("SSR_MAX_TEMP", _config->ssr_max_temp);
        _config->force_equipment = getBool("FORCE_EQUIPMENT");
        _config->e_force_window = getBool("E_FORCE_WINDOW");
        _config->force_start = getParam("FORCE_START");
        _config->force_end = getParam("FORCE_END");
        _config->night_start = getParam("NIGHT_START");
        _config->night_end = getParam("NIGHT_END");
        _config->night_poll_interval = getInt("NIGHT_POLL_INTERVAL", _config->night_poll_interval);
        _config->e_jsy = getBool("E_JSY");
        _config->jsy_uart_id = getInt("JSY_UART_ID", _config->jsy_uart_id);
        _config->jsy_tx = getInt("JSY_TX", _config->jsy_tx);
        _config->jsy_rx = getInt("JSY_RX", _config->jsy_rx);
        _config->zx_pin = getInt("ZX_PIN", _config->zx_pin);
        _config->control_mode = getParam("CONTROL_MODE").length() > 0 ? getParam("CONTROL_MODE") : _config->control_mode;
        _config->e_fan = getBool("E_FAN");
        _config->fan_pin = getInt("FAN_PIN", _config->fan_pin);
        _config->fan_temp_offset = getInt("FAN_TEMP_OFFSET", _config->fan_temp_offset);
        _config->internal_led_pin = getInt("I_LED_PIN", _config->internal_led_pin);
        _config->ssr_pin = getInt("SSR_PIN", _config->ssr_pin);
        _config->relay_pin = getInt("RELAY_PIN", _config->relay_pin);

        if (ConfigManager::save(*_config)) {
            request->send(200, "text/html", "<html><body><h1>Configuration saved</h1><p>Device will reboot...</p></body></html>");
            _rebootRequested = true;
        } else {
            request->send(500, "text/plain", "Error saving config");
        }
    });

    static File uploadFile;
    _server.on("/import_stats", HTTP_POST, [authRequired](AsyncWebServerRequest *request) {
        if (!authRequired(request)) return;
        request->send(200, "text/plain", "Importation réussie. Redémarrage...");
        _rebootRequested = true;
    }, [authRequired](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
        if (!authRequired(request)) return;
        if (!index) {
            Logger::info("Importation des stats : " + filename);
            uploadFile = LittleFS.open("/stats.json", "w");
        }
        if (uploadFile) {
            if (len) uploadFile.write(data, len);
            if (final) uploadFile.close();
        }
    });

    _server.on("/update", HTTP_POST, [authRequired](AsyncWebServerRequest *request) {
        if (!authRequired(request)) return;
        bool shouldReboot = !Update.hasError();
        AsyncWebServerResponse *response = request->beginResponse(200, "text/plain", shouldReboot ? "OK" : "FAIL");
        response->addHeader("Connection", "close");
        request->send(response);
        if (shouldReboot) _rebootRequested = true;
    }, [authRequired](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
        if (!authRequired(request)) return;
        if (!index) {
            int command = (filename.indexOf("spiffs") > -1 || filename.indexOf("littlefs") > -1) ? U_SPIFFS : U_FLASH;
            Update.begin(UPDATE_SIZE_UNKNOWN, command);
        }
        if (!Update.hasError()) Update.write(data, len);
        if (final) Update.end(true);
    });

    _server.onNotFound([](AsyncWebServerRequest *request) {
        if (WiFi.getMode() == WIFI_AP) request->redirect("http://" + WiFi.softAPIP().toString() + "/");
        else request->send(404, "text/plain", "Not found");
    });
}

String WebManager::getStatusJson() {
    JsonDocument doc;
    doc["grid_power"] = GridSensorService::currentGridPower;
    doc["grid_voltage"] = GridSensorService::currentGridVoltage;
    doc["equipment_power"] = ActuatorManager::equipmentPower;
    doc["equipment_active"] = ActuatorManager::equipmentActive;
    doc["force_mode"] = (SafetyManager::currentState == SystemState::STATE_BOOST);
    doc["boost_active"] = (millis() / 1000) < ActuatorManager::boostEndTime;
    
    // Map State Machine to UI legacy flags
    doc["safe_state"] = (SafetyManager::currentState == SystemState::STATE_SAFE_TIMEOUT);
    doc["emergency_mode"] = (SafetyManager::currentState == SystemState::STATE_EMERGENCY_FAULT);
    doc["emergency_reason"] = SafetyManager::emergencyReason;
    doc["ssr_temp"] = (TemperatureManager::currentSsrTemp > -100.0) ? (float)TemperatureManager::currentSsrTemp : JsonVariant();
    doc["fan_active"] = ActuatorManager::fanActive;
    doc["fan_percent"] = ActuatorManager::fanPercent;
    doc["total_import"] = StatsManager::totalImportToday;
    doc["total_redirect"] = StatsManager::totalRedirectToday;
    doc["total_export"] = StatsManager::totalExportToday;
    doc["free_ram"] = Utils::getFreeHeap();
    doc["total_ram"] = Utils::getTotalHeap();
    doc["free_psram"] = Utils::getFreePsram();
    doc["total_psram"] = Utils::getTotalPsram();
    doc["uptime"] = millis() / 1000;
    doc["rssi"] = NetworkManager::getRSSI();

    doc["esp_temp"] = TemperatureManager::lastEspTemp;

    doc["grid_source"] = _config->e_shelly_mqtt ? "MQTT" : "HTTP";
    doc["shelly_link"] = (SafetyManager::currentState != SystemState::STATE_SAFE_TIMEOUT);
    doc["shelly_error"] = (SafetyManager::currentState == SystemState::STATE_SAFE_TIMEOUT);
    doc["shelly_mqtt_enabled"] = _config->e_shelly_mqtt;
    doc["mqtt_enabled"] = _config->e_mqtt;
    doc["mqtt_status"] = MqttManager::isConnected();
    doc["e_ssr_temp"] = _config->e_ssr_temp;
    doc["night_mode"] = SolarMonitor::isNight(Utils::getCurrentMinutes());

    time_t now; time(&now); struct tm ti; localtime_r(&now, &ti);
    char buf[6]; strftime(buf, sizeof(buf), "%H:%M", &ti);
    doc["rtc_time"] = String(buf);
    char buf2[11]; strftime(buf2, sizeof(buf2), "%d/%m/%Y", &ti);
    doc["rtc_date"] = String(buf2);

    String output;
    serializeJson(doc, output);
    return output;
}

String WebManager::getHistoryJson() {
    return HistoryBuffer::getHistoryJson();
}
