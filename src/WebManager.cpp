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
#include <HTTPClient.h>
#include <LittleFS.h>
#include <Update.h>
#include <ArduinoJson.h>
AsyncWebServer WebManager::_server(80);
WiFiClient WebManager::_client;
HTTPClient WebManager::_http;
const Config* WebManager::_config = nullptr;
bool WebManager::_rebootRequested = false;

void WebManager::init(const Config& config) {
    _config = &config;
    _http.setConnectTimeout(1000);
    _http.setTimeout(2000);
    setupRoutes();
    _server.begin();
    Logger::info("Web Server started");
}

void WebManager::loop() {
    if (_rebootRequested) {
        Logger::info("Reboot requested. Saving data...");
        Logger::flushAll(); // Save recent logs
        
#ifndef DISABLE_STATS
        esp_task_wdt_reset();
        StatsManager::save();   // Save daily stats
        esp_task_wdt_reset();
        HistoryBuffer::save(); // Save chart history
#endif

        delay(1000);
        Utils::reboot();
    }
}

void WebManager::applyRequestParams(AsyncWebServerRequest *request, Config &cfg) {
    auto has = [&](const char* name) { return request->hasParam(name, true); };
    auto get = [&](const char* name) { return request->getParam(name, true)->value(); };

    // System
    if (has("NAME")) cfg.name = get("NAME");
    if (has("TIMEZONE")) cfg.timezone = get("TIMEZONE");
    if (has("CPU_FREQ")) cfg.cpu_freq = get("CPU_FREQ").toInt();
    if (has("MAX_ESP32_TEMP")) cfg.max_esp32_temp = get("MAX_ESP32_TEMP").toFloat();

    // WiFi
    if (has("E_WIFI")) cfg.e_wifi = (get("E_WIFI") == "True");
    if (has("WIFI_SSID")) cfg.wifi_ssid = get("WIFI_SSID");
    if (has("WIFI_PASSWORD")) cfg.wifi_password = get("WIFI_PASSWORD");
    if (has("WIFI_STATIC_IP")) cfg.wifi_static_ip = get("WIFI_STATIC_IP");
    if (has("WIFI_SUBNET")) cfg.wifi_subnet = get("WIFI_SUBNET");
    if (has("WIFI_GATEWAY")) cfg.wifi_gateway = get("WIFI_GATEWAY");
    if (has("WIFI_DNS")) cfg.wifi_dns = get("WIFI_DNS");

    // Shelly / Grid
    if (has("SHELLY_EM_IP")) cfg.shelly_em_ip = get("SHELLY_EM_IP");
    if (has("SHELLY_EM_INDEX")) cfg.shelly_em_index = get("SHELLY_EM_INDEX").toInt();
    if (has("E_SHELLY_MQTT")) cfg.e_shelly_mqtt = (get("E_SHELLY_MQTT") == "True");
    if (has("SHELLY_MQTT_TOPIC")) cfg.shelly_mqtt_topic = get("SHELLY_MQTT_TOPIC");
    if (has("POLL_INTERVAL")) cfg.poll_interval = get("POLL_INTERVAL").toInt();
    if (has("SHELLY_TIMEOUT")) cfg.shelly_timeout = get("SHELLY_TIMEOUT").toInt();
    if (has("SAFETY_TIMEOUT")) cfg.safety_timeout = get("SAFETY_TIMEOUT").toInt();
    if (has("FAKE_SHELLY")) cfg.fake_shelly = (get("FAKE_SHELLY") == "True");

    // Equipment 1
    if (has("EQUIP1_NAME")) cfg.equip1_name = get("EQUIP1_NAME");
    if (has("EQUIP1_MAX_POWER")) cfg.equip1_max_power = get("EQUIP1_MAX_POWER").toFloat();
    if (has("E_EQUIP1")) cfg.e_equip1 = (get("E_EQUIP1") == "True");
    if (has("EQUIP1_SHELLY_IP")) cfg.equip1_shelly_ip = get("EQUIP1_SHELLY_IP");
    if (has("EQUIP1_SHELLY_INDEX")) cfg.equip1_shelly_index = get("EQUIP1_SHELLY_INDEX").toInt();
    if (has("E_EQUIP1_MQTT")) cfg.e_equip1_mqtt = (get("E_EQUIP1_MQTT") == "True");
    if (has("EQUIP1_MQTT_TOPIC")) cfg.equip1_mqtt_topic = get("EQUIP1_MQTT_TOPIC");

    // Equipment 2
    if (has("E_EQUIP2")) cfg.e_equip2 = (get("E_EQUIP2") == "True");
    if (has("EQUIP2_NAME")) cfg.equip2_name = get("EQUIP2_NAME");
    if (has("EQUIP2_IP")) cfg.equip2_shelly_ip = get("EQUIP2_IP");
    if (has("EQUIP2_SHELLY_INDEX")) cfg.equip2_shelly_index = get("EQUIP2_SHELLY_INDEX").toInt();
    if (has("E_EQUIP2_MQTT")) cfg.e_equip2_mqtt = (get("E_EQUIP2_MQTT") == "True");
    if (has("EQUIP2_MQTT_TOPIC")) cfg.equip2_mqtt_topic = get("EQUIP2_MQTT_TOPIC");
    if (has("EQUIP2_POWER")) cfg.equip2_max_power = get("EQUIP2_POWER").toFloat();
    if (has("EQUIP2_PRIO")) cfg.equip2_priority = get("EQUIP2_PRIO").toInt();
    if (has("EQUIP2_MIN_TIME")) cfg.equip2_min_on_time = get("EQUIP2_MIN_TIME").toInt();

    // Strategy / PID
    if (has("DELTA")) cfg.delta = get("DELTA").toFloat();
    if (has("DELTANEG")) cfg.deltaneg = get("DELTANEG").toFloat();
    if (has("COMPENSATION")) cfg.compensation = get("COMPENSATION").toFloat();
    if (has("DYNAMIC_THRESHOLD_W")) cfg.dynamic_threshold_w = get("DYNAMIC_THRESHOLD_W").toFloat();
    if (has("EXPORT_SETPOINT")) cfg.export_setpoint = get("EXPORT_SETPOINT").toFloat();
    if (has("MAX_DUTY_PERCENT")) cfg.max_duty_percent = get("MAX_DUTY_PERCENT").toFloat();

    // Hardware
    if (has("SSR_PIN")) cfg.ssr_pin = get("SSR_PIN").toInt();
    if (has("RELAY_PIN")) cfg.relay_pin = get("RELAY_PIN").toInt();
    if (has("DS18B20_PIN")) cfg.ds18b20_pin = get("DS18B20_PIN").toInt();
    if (has("I_LED_PIN")) cfg.internal_led_pin = get("I_LED_PIN").toInt();
    if (has("FAN_PIN")) cfg.fan_pin = get("FAN_PIN").toInt();
    if (has("FAN_TEMP_OFFSET")) cfg.fan_temp_offset = get("FAN_TEMP_OFFSET").toInt();
    if (has("E_FAN")) cfg.e_fan = (get("E_FAN") == "True");
    if (has("E_SSR_TEMP")) cfg.e_ssr_temp = (get("E_SSR_TEMP") == "True");
    if (has("SSR_MAX_TEMP")) cfg.ssr_max_temp = get("SSR_MAX_TEMP").toFloat();
    if (has("ZX_PIN")) cfg.zx_pin = get("ZX_PIN").toInt();
    if (has("CONTROL_MODE")) cfg.control_mode = get("CONTROL_MODE");

    // Force / Night
    if (has("FORCE_EQUIPMENT")) cfg.force_equipment = (get("FORCE_EQUIPMENT") == "True");
    if (has("E_FORCE_WINDOW")) cfg.e_force_window = (get("E_FORCE_WINDOW") == "True");
    if (has("FORCE_START")) cfg.force_start = get("FORCE_START");
    if (has("FORCE_END")) cfg.force_end = get("FORCE_END");
    if (has("NIGHT_POLL_INTERVAL")) cfg.night_poll_interval = get("NIGHT_POLL_INTERVAL").toInt();

    // MQTT
    if (has("E_MQTT")) cfg.e_mqtt = (get("E_MQTT") == "True");
    if (has("MQTT_IP")) cfg.mqtt_ip = get("MQTT_IP");
    if (has("MQTT_PORT")) cfg.mqtt_port = get("MQTT_PORT").toInt();
    if (has("MQTT_USER")) cfg.mqtt_user = get("MQTT_USER");
    if (has("MQTT_PASSWORD")) cfg.mqtt_password = get("MQTT_PASSWORD");
    if (has("MQTT_NAME")) cfg.mqtt_name = get("MQTT_NAME");

    // Weather
    if (has("E_WEATHER")) cfg.e_weather = (get("E_WEATHER") == "True");
    if (has("WEATHER_LAT")) cfg.weather_lat = get("WEATHER_LAT");
    if (has("WEATHER_LON")) cfg.weather_lon = get("WEATHER_LON");
    if (has("WEATHER_THRESH")) cfg.weather_cloud_threshold = get("WEATHER_THRESH").toInt();

    // Web
    if (has("WEB_USER")) cfg.web_user = get("WEB_USER");
    if (has("WEB_PASSWORD")) cfg.web_password = get("WEB_PASSWORD");
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
        AsyncWebServerResponse *response = request->beginResponse(LittleFS, "/web_command.html.gz", "text/html");
        response->addHeader("Content-Encoding", "gzip");
        request->send(response);
    });

    auto sendLogSnapshot = [authRequired](AsyncWebServerRequest *request, const char* path, const char* downloadName, const char* missingMessage) {
        if (!authRequired(request)) return;

        if (!Logger::getMutex() || xSemaphoreTakeRecursive(Logger::getMutex(), pdMS_TO_TICKS(2000)) != pdTRUE) {
            request->send(503, "text/plain", "System busy, try again later");
            return;
        }

        Logger::flushAll();

        if (!LittleFS.exists(path)) {
            xSemaphoreGiveRecursive(Logger::getMutex());
            request->send(404, "text/plain", missingMessage);
            return;
        }

        AsyncResponseStream *response = request->beginResponseStream("text/plain");
        response->addHeader("Content-Disposition", String("attachment; filename=") + downloadName);
        response->addHeader("Cache-Control", "no-store");

        File file = LittleFS.open(path, "r");
        if (file) {
            uint8_t buffer[512];
            while (file.available()) {
                size_t len = file.read(buffer, sizeof(buffer));
                response->write(buffer, len);
            }
            file.close();
        }

        xSemaphoreGiveRecursive(Logger::getMutex());
        request->send(response);
    };

    _server.on("/download_logs", HTTP_GET, [authRequired, sendLogSnapshot](AsyncWebServerRequest *request) {
        sendLogSnapshot(request, "/log.txt", "solar_log.txt", "Log file not found");
    });

    _server.on("/download_data", HTTP_GET, [authRequired, sendLogSnapshot](AsyncWebServerRequest *request) {
        sendLogSnapshot(request, "/solar_data.txt", "solar_data.txt", "Data file not found");
    });

    _server.on("/get_log_action", HTTP_GET, [](AsyncWebServerRequest *request) {
        Logger::streamLogs(request);
    });

    _server.on("/get_solar_data", HTTP_GET, [](AsyncWebServerRequest *request) {
        Logger::streamDataLogs(request);
    });

    _server.on("/weather_refresh", HTTP_GET, [](AsyncWebServerRequest *request) {
        WeatherManager::forceUpdate();
        request->send(200, "text/plain", "OK");
    });

    _server.on("/uPlot.iife.min.js", HTTP_GET, [](AsyncWebServerRequest *request) {
        AsyncWebServerResponse *response = request->beginResponse(LittleFS, "/uPlot.iife.min.js.gz", "application/javascript");
        response->addHeader("Content-Encoding", "gzip");
        response->addHeader("Cache-Control", "public, max-age=86400");
        request->send(response);
    });
    _server.on("/uPlot.min.css", HTTP_GET, [](AsyncWebServerRequest *request) {
        AsyncWebServerResponse *response = request->beginResponse(LittleFS, "/uPlot.min.css.gz", "text/css");
        response->addHeader("Content-Encoding", "gzip");
        response->addHeader("Cache-Control", "public, max-age=86400");
        request->send(response);
    });
    _server.on("/help.json", HTTP_GET, [](AsyncWebServerRequest *request) {
        AsyncWebServerResponse *response = request->beginResponse(LittleFS, "/help.json.gz", "application/json");
        response->addHeader("Content-Encoding", "gzip");
        response->addHeader("Cache-Control", "public, max-age=86400");
        request->send(response);
    });
    _server.serveStatic("/icons", LittleFS, "/icons");

#ifndef DISABLE_STATS
    _server.on("/stats", HTTP_GET, [authRequired](AsyncWebServerRequest *request) {
        if (!authRequired(request)) return;
        AsyncWebServerResponse *response = request->beginResponse(LittleFS, "/web_stats.html.gz", "text/html");
        response->addHeader("Content-Encoding", "gzip");
        request->send(response);
    });
    _server.on("/get_stats", HTTP_GET, [authRequired](AsyncWebServerRequest *request) {
        if (!authRequired(request)) return;
        StatsManager::streamStatsJson(request);
    });

    _server.on("/import_stats", HTTP_POST, [authRequired](AsyncWebServerRequest *request) {
        if (!authRequired(request)) return;
        request->send(200, "text/plain", "Importation réussie. Redémarrage...");
        _rebootRequested = true;
    }, [authRequired](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
        if (!authRequired(request)) return;
        static File uploadFile;
        static size_t uploadedBytes = 0;
        static constexpr size_t MAX_STATS_UPLOAD_BYTES = 200 * 1024;
        
        if (!index) {
            uploadedBytes = 0;
            if (uploadFile) uploadFile.close();
            Logger::info("Importing stats: " + filename);
            uploadFile = LittleFS.open("/stats.json", "w");
        }
        
        if (uploadFile) {
            uploadedBytes += len;
            if (uploadedBytes > MAX_STATS_UPLOAD_BYTES) {
                uploadFile.close();
                LittleFS.remove("/stats.json");
                Logger::error("Stats upload rejected: file too large");
                return;
            }
            if (len) uploadFile.write(data, len);
            if (final) {
                uploadFile.close();
                Logger::info("Stats upload complete (" + String(uploadedBytes) + " bytes)");
            }
        }
    });
#endif

    _server.on("/web_config", HTTP_GET, [authRequired](AsyncWebServerRequest *request) {
        if (!authRequired(request)) return;
        AsyncWebServerResponse *response = request->beginResponse(LittleFS, "/web_config.html.gz", "text/html");
        response->addHeader("Content-Encoding", "gzip");
        request->send(response);
    });

    _server.on("/web_equip2", HTTP_GET, [authRequired](AsyncWebServerRequest *request) {
        if (!authRequired(request)) return;
        AsyncWebServerResponse *response = request->beginResponse(LittleFS, "/web_equip2.html.gz", "text/html");
        response->addHeader("Content-Encoding", "gzip");
        request->send(response);
    });

    _server.on("/save_config_eq2", HTTP_POST, [authRequired](AsyncWebServerRequest *request) {
        if (!authRequired(request)) return;
        
        Config newCfg = *_config;
        applyRequestParams(request, newCfg);
        
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
        doc["equip2_name"] = _config->equip2_name;
        doc["equip2_schedule"] = String((uint64_t)_config->equip2_schedule);
        String out;
        serializeJson(doc, out);
        request->send(200, "application/json", out);
    });


    _server.on("/save_config", HTTP_POST, [authRequired](AsyncWebServerRequest *request) {
        if (!authRequired(request)) return;
        
        Config newCfg = *_config;
        applyRequestParams(request, newCfg);

        if (ConfigManager::save(newCfg)) {
            request->send(200, "text/plain", "Configuration sauvegardée. Redémarrage...");
            _rebootRequested = true;
        } else {
            request->send(500, "text/plain", "Erreur lors de la sauvegarde");
        }
    });

    _server.on("/get_config", HTTP_GET, [authRequired](AsyncWebServerRequest *request) {
        if (!authRequired(request)) return;
        JsonDocument doc;
        doc["name"] = _config->name;
        doc["equip1_name"] = _config->equip1_name;
        doc["timezone"] = _config->timezone;
        doc["cpu_freq"] = _config->cpu_freq;
        doc["e_wifi"] = _config->e_wifi;
        doc["wifi_ssid"] = _config->wifi_ssid;
        doc["wifi_password"] = _config->wifi_password;
        doc["wifi_static_ip"] = _config->wifi_static_ip;
        doc["wifi_subnet"] = _config->wifi_subnet;
        doc["wifi_gateway"] = _config->wifi_gateway;
        doc["wifi_dns"] = _config->wifi_dns;
        doc["shelly_em_ip"] = _config->shelly_em_ip;
        doc["shelly_em_index"] = _config->shelly_em_index;
        doc["e_shelly_mqtt"] = _config->e_shelly_mqtt;
        doc["shelly_mqtt_topic"] = _config->shelly_mqtt_topic;
        doc["poll_interval"] = _config->poll_interval;
        doc["shelly_timeout"] = _config->shelly_timeout;
        doc["safety_timeout"] = _config->safety_timeout;
        doc["e_mqtt"] = _config->e_mqtt;
        doc["mqtt_ip"] = _config->mqtt_ip;
        doc["mqtt_port"] = _config->mqtt_port;
        doc["mqtt_user"] = _config->mqtt_user;
        doc["mqtt_password"] = _config->mqtt_password;
        doc["mqtt_name"] = _config->mqtt_name;
        doc["equip1_max_power"] = _config->equip1_max_power;
        doc["max_duty_percent"] = _config->max_duty_percent;
        doc["export_setpoint"] = _config->export_setpoint;
        doc["e_equip1"] = _config->e_equip1;
        doc["equip1_shelly_ip"] = _config->equip1_shelly_ip;
        doc["equip1_shelly_index"] = _config->equip1_shelly_index;
        doc["e_equip1_mqtt"] = _config->e_equip1_mqtt;
        doc["equip1_mqtt_topic"] = _config->equip1_mqtt_topic;
        doc["delta"] = _config->delta;
        doc["deltaneg"] = _config->deltaneg;
        doc["compensation"] = _config->compensation;
        doc["dynamic_threshold_w"] = _config->dynamic_threshold_w;
        doc["ds18b20_pin"] = _config->ds18b20_pin;
        doc["force_equipment"] = _config->force_equipment;
        doc["e_force_window"] = _config->e_force_window;
        doc["force_start"] = _config->force_start;
        doc["force_end"] = _config->force_end;
        doc["night_poll_interval"] = _config->night_poll_interval;
        doc["e_jsy"] = _config->e_jsy;
        doc["jsy_uart_id"] = _config->jsy_uart_id;
        doc["jsy_tx"] = _config->jsy_tx;
        doc["jsy_rx"] = _config->jsy_rx;
        doc["zx_pin"] = _config->zx_pin;
        doc["control_mode"] = _config->control_mode;
        doc["e_fan"] = _config->e_fan;
        doc["fan_pin"] = _config->fan_pin;
        doc["fan_temp_offset"] = _config->fan_temp_offset;
        doc["e_ssr_temp"] = _config->e_ssr_temp;
        doc["ssr_max_temp"] = _config->ssr_max_temp;
        doc["internal_led_pin"] = _config->internal_led_pin;
        doc["ssr_pin"] = _config->ssr_pin;
        doc["relay_pin"] = _config->relay_pin;
        doc["e_weather"] = _config->e_weather;
        doc["weather_lat"] = _config->weather_lat;
        doc["weather_lon"] = _config->weather_lon;
        doc["weather_cloud_threshold"] = _config->weather_cloud_threshold;
        doc["fake_shelly"] = _config->fake_shelly;
        doc["web_user"] = _config->web_user;
        doc["web_password"] = _config->web_password;
        doc["e_equip2"] = _config->e_equip2;
        doc["equip2_name"] = _config->equip2_name;
        doc["equip2_shelly_ip"] = _config->equip2_shelly_ip;
        doc["equip2_shelly_index"] = _config->equip2_shelly_index;
        doc["e_equip2_mqtt"] = _config->e_equip2_mqtt;
        doc["equip2_mqtt_topic"] = _config->equip2_mqtt_topic;
        doc["equip2_max_power"] = _config->equip2_max_power;
        doc["equip2_priority"] = _config->equip2_priority;
        doc["equip2_min_on_time"] = _config->equip2_min_on_time;
        doc["equip2_schedule"] = String((uint64_t)_config->equip2_schedule);
        String out;
        serializeJson(doc, out);
        request->send(200, "application/json", out);
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
        int minutes = request->hasParam("min", true) ? request->getParam("min", true)->value().toInt() : -1;
        ActuatorManager::startBoost(minutes);
        request->redirect("/");
    });

    _server.on("/cancel_boost", HTTP_POST, [authRequired](AsyncWebServerRequest *request) {
        if (!authRequired(request)) return;
        Logger::info("Manual boost cancelled");
        ActuatorManager::cancelBoost();
        request->redirect("/");
    });

    _server.on("/test_shelly", HTTP_POST, [](AsyncWebServerRequest *request) {
        String target = request->hasParam("target", true) ? request->getParam("target", true)->value() : "";
        String ip;
        int index = 0;
        bool isEM = false;

        if (target == "em") {
            ip = _config->shelly_em_ip;
            index = _config->shelly_em_index;
            isEM = true;
        } else if (target == "eq1") {
            ip = _config->equip1_shelly_ip;
            index = _config->equip1_shelly_index;
        } else if (target == "eq2") {
            ip = _config->equip2_shelly_ip;
            index = _config->equip2_shelly_index;
        } else {
            request->send(400, "application/json", "{\"ok\":false,\"error\":\"Invalid target\"}");
            return;
        }

        if (ip.length() == 0) {
            request->send(200, "application/json", "{\"ok\":false,\"error\":\"IP non configurée\"}");
            return;
        }

        String result;
        if (isEM) {
            String url = "http://" + ip + "/emeter/" + String(index);
            _http.begin(_client, url);
            int code = _http.GET();
            if (code == 200) {
                JsonDocument doc;
                if (!deserializeJson(doc, _http.getStream())) {
                    float power = doc["power"] | 0.0f;
                    float voltage = doc["voltage"] | 0.0f;
                    result = "{\"ok\":true,\"power\":" + String(power, 1) + ",\"voltage\":" + String(voltage, 1) + ",\"gen\":\"EM\"}";
                } else {
                    result = "{\"ok\":false,\"error\":\"JSON parse error\"}";
                }
            } else {
                result = "{\"ok\":false,\"error\":\"HTTP " + String(code) + "\"}";
            }
            _http.end();
        } else {
            String url = "http://" + ip + "/rpc/Switch.GetStatus?id=" + String(index);
            _http.begin(_client, url);
            int code = _http.GET();
            if (code == 200) {
                JsonDocument doc;
                if (!deserializeJson(doc, _http.getStream())) {
                    float power = doc["apower"] | 0.0f;
                    bool relay = doc["output"] | false;
                    result = "{\"ok\":true,\"power\":" + String(power, 1) + ",\"gen\":\"Gen2\",\"relay\":" + (relay ? "true" : "false") + "}";
                } else {
                    result = "{\"ok\":false,\"error\":\"JSON parse error (Gen2)\"}";
                }
                _http.end();
            } else {
                _http.end();
                url = "http://" + ip + "/status";
                _http.begin(_client, url);
                code = _http.GET();
                if (code == 200) {
                    JsonDocument doc;
                    if (!deserializeJson(doc, _http.getStream())) {
                        float power = 0;
                        if (doc.containsKey("meters")) power = doc["meters"][index]["power"] | 0.0f;
                        else if (doc.containsKey("emeters")) power = doc["emeters"][index]["power"] | 0.0f;
                        bool relay = doc["relays"][0]["ison"] | false;
                        result = "{\"ok\":true,\"power\":" + String(power, 1) + ",\"gen\":\"Gen1\",\"relay\":" + (relay ? "true" : "false") + "}";
                    } else {
                        result = "{\"ok\":false,\"error\":\"JSON parse error (Gen1)\"}";
                    }
                } else {
                    result = "{\"ok\":false,\"error\":\"HTTP " + String(code) + " (Gen1+Gen2)\"}";
                }
                _http.end();
            }
        }
        request->send(200, "application/json", result);
    });

    _server.on("/status", HTTP_GET, [](AsyncWebServerRequest *request) {
        streamStatusJson(request);
    });

    _server.on("/history", HTTP_GET, [](AsyncWebServerRequest *request) {
        HistoryBuffer::streamHistoryJson(request);
    });
}

void WebManager::streamStatusJson(AsyncWebServerRequest *request) {
    AsyncResponseStream *response = request->beginResponseStream("application/json");
    JsonDocument doc;
    doc["grid_power"] = GridSensorService::currentGridPower;
    doc["equipment_power"] = ActuatorManager::equipmentPower;
    doc["eq1_real_power"] = Shelly1PMManager::getPowerEq1();
    doc["equip2_power"] = Shelly1PMManager::getPower();
    doc["boost_active"] = (SafetyManager::currentState == SystemState::STATE_BOOST);
    doc["force_mode"] = (SafetyManager::currentState == SystemState::STATE_BOOST);
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
    static uint32_t lastHealthUpdate = 0;
    static uint32_t cachedFreeRam = 0, cachedTotalRam = 0;
    static uint32_t cachedFreePsram = 0, cachedTotalPsram = 0;
    static int cachedRssi = -100;
    static float cachedEspTemp = 0;
    uint32_t now = millis();
    if (now - lastHealthUpdate >= 10000 || lastHealthUpdate == 0) {
        lastHealthUpdate = now;
        cachedFreeRam = Utils::getFreeHeap();
        cachedTotalRam = Utils::getTotalHeap();
        cachedFreePsram = Utils::getFreePsram();
        cachedTotalPsram = Utils::getTotalPsram();
        cachedRssi = NetworkManager::getRSSI();
        cachedEspTemp = TemperatureManager::lastEspTemp;
    }
    doc["free_ram"] = cachedFreeRam;
    doc["total_ram"] = cachedTotalRam;
    doc["free_psram"] = cachedFreePsram;
    doc["total_psram"] = cachedTotalPsram;
    doc["uptime"] = now / 1000;
    doc["rssi"] = cachedRssi;
    doc["version"] = String(FIRMWARE_VERSION);
    static const String buildTime = String(__DATE__) + " " + String(__TIME__);
    doc["build_time"] = buildTime;

    doc["esp_temp"] = cachedEspTemp;

    doc["grid_source"] = _config->e_shelly_mqtt ? "MQTT" : "HTTP";
    doc["shelly_link"] = (SafetyManager::currentState != SystemState::STATE_SAFE_TIMEOUT);
    doc["shelly_error"] = (SafetyManager::currentState == SystemState::STATE_SAFE_TIMEOUT);
    doc["shelly_mqtt_enabled"] = _config->e_shelly_mqtt;
    doc["mqtt_enabled"] = _config->e_mqtt;
    doc["mqtt_status"] = MqttManager::isConnected();
    doc["e_ssr_temp"] = _config->e_ssr_temp;
    doc["e_equip1"] = _config->e_equip1;
    doc["e_equip2"] = _config->e_equip2;
    doc["equip1_name"] = _config->equip1_name;
    doc["equip2_name"] = _config->equip2_name;
#ifdef DISABLE_STATS
    doc["stats_enabled"] = false;
#else
    doc["stats_enabled"] = true;
#endif
#ifdef DISABLE_HISTORY
    doc["history_enabled"] = false;
#else
    doc["history_enabled"] = true;
#endif
#ifdef DISABLE_DATA_LOG
    doc["data_log_enabled"] = false;
#else
    doc["data_log_enabled"] = true;
#endif
#ifdef MAX_STATS_DAYS
    doc["max_stats_days"] = MAX_STATS_DAYS;
#else
    doc["max_stats_days"] = 30;
#endif
    doc["equip2_bypassed"] = Equipment2Manager::isBypassedByCloud();
    doc["night_mode"] = SolarMonitor::isNight(Utils::getCurrentMinutes());

    // Weather
    doc["e_weather"] = _config->e_weather;
    if (_config->e_weather) {
        doc["weather_clouds"] = WeatherManager::getCloudCover();
        doc["weather_clouds_low"] = WeatherManager::getCloudCoverLow();
        doc["weather_clouds_mid"] = WeatherManager::getCloudCoverMid();
        doc["weather_clouds_high"] = WeatherManager::getCloudCoverHigh();
        doc["weather_effective_clouds"] = WeatherManager::getEffectiveCloudiness();
        doc["weather_solar_confidence"] = WeatherManager::getSolarConfidence();
        doc["weather_shortwave_radiation"] = WeatherManager::getShortwaveRadiationInstant();
        doc["weather_terrestrial_radiation"] = WeatherManager::getTerrestrialRadiationInstant();
        doc["weather_temp"] = WeatherManager::getTemperature();
        doc["weather_rain"] = WeatherManager::getRain();
        doc["weather_snow"] = WeatherManager::getSnow();
        doc["weather_sunrise"] = WeatherManager::getSunrise();
        doc["weather_sunset"] = WeatherManager::getSunset();
        doc["weather_age"] = millis() - WeatherManager::getLastUpdate();
        doc["weather_icon"] = WeatherManager::getWeatherIcon();
        doc["weather_too_cloudy"] = WeatherManager::isTooCloudy();
    }

    time_t t_now; time(&t_now); struct tm ti; localtime_r(&t_now, &ti);
    char tbuf[12]; strftime(tbuf, sizeof(tbuf), "%H:%M", &ti);
    doc["rtc_time"] = String(tbuf);
    char tbuf2[24]; strftime(tbuf2, sizeof(tbuf2), "%d/%m/%Y", &ti);
    doc["rtc_date"] = String(tbuf2);

    serializeJson(doc, *response);
    request->send(response);
}

String WebManager::getHistoryJson() {
    return HistoryBuffer::getHistoryJson();
}
