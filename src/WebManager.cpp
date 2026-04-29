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
        esp_task_wdt_reset();
        StatsManager::save();   // Save daily stats
        esp_task_wdt_reset();
        HistoryBuffer::save(); // Save chart history
#endif

        delay(1000);
        Utils::reboot();
    }
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
        
        auto getParam = [&](const char* name) -> String {
            return request->hasParam(name, true) ? request->getParam(name, true)->value() : String();
        };

        Config newCfg = *_config;
        newCfg.e_equip2 = (getParam("E_EQUIP2") == "True");
        newCfg.equip2_name = getParam("EQUIP2_NAME");
        newCfg.equip2_shelly_ip = getParam("EQUIP2_IP");
        newCfg.equip2_shelly_index = getParam("EQUIP2_SHELLY_INDEX").toInt();
        newCfg.e_equip2_mqtt = (getParam("E_EQUIP2_MQTT") == "True");
        newCfg.equip2_mqtt_topic = getParam("EQUIP2_MQTT_TOPIC");
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
        doc["equip2_name"] = _config->equip2_name;
        doc["equip2_schedule"] = String((uint64_t)_config->equip2_schedule);
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
        newCfg.equip1_name = getParam("EQUIP1_NAME");
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
        newCfg.equip2_shelly_index = getParam("EQUIP2_SHELLY_INDEX").toInt();
        newCfg.e_equip2_mqtt = (getParam("E_EQUIP2_MQTT") == "True");
        newCfg.equip2_mqtt_topic = getParam("EQUIP2_MQTT_TOPIC");
        newCfg.equip2_max_power = getParam("EQUIP2_POWER").toFloat();
        newCfg.equip2_priority = getParam("EQUIP2_PRIO").toInt();
        newCfg.equip2_min_on_time = getParam("EQUIP2_MIN_TIME").toInt();
        
        newCfg.shelly_em_ip = getParam("SHELLY_EM_IP");
        newCfg.shelly_em_index = getParam("SHELLY_EM_INDEX").toInt();
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

        newCfg.equip1_name = getParam("EQUIP1_NAME");
        newCfg.equip1_max_power = getParam("EQUIP1_MAX_POWER").toFloat();
        newCfg.e_equip1 = (getParam("E_EQUIP1") == "True");
        newCfg.equip1_shelly_ip = getParam("EQUIP1_SHELLY_IP");
        newCfg.equip1_shelly_index = getParam("EQUIP1_SHELLY_INDEX").toInt();
        newCfg.e_equip1_mqtt = (getParam("E_EQUIP1_MQTT") == "True");
        newCfg.equip1_mqtt_topic = getParam("EQUIP1_MQTT_TOPIC");
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

        WiFiClient client;
        HTTPClient http;
        http.setConnectTimeout(1000);
        http.setTimeout(2000);

        String result;
        if (isEM) {
            String url = "http://" + ip + "/emeter/" + String(index);
            http.begin(client, url);
            int code = http.GET();
            if (code == 200) {
                JsonDocument doc;
                if (!deserializeJson(doc, http.getStream())) {
                    float power = doc["power"] | 0.0f;
                    float voltage = doc["voltage"] | 0.0f;
                    result = "{\"ok\":true,\"power\":" + String(power, 1) + ",\"voltage\":" + String(voltage, 1) + ",\"gen\":\"EM\"}";
                } else {
                    result = "{\"ok\":false,\"error\":\"JSON parse error\"}";
                }
            } else {
                result = "{\"ok\":false,\"error\":\"HTTP " + String(code) + "\"}";
            }
            http.end();
        } else {
            String url = "http://" + ip + "/rpc/Switch.GetStatus?id=" + String(index);
            http.begin(client, url);
            int code = http.GET();
            if (code == 200) {
                JsonDocument doc;
                if (!deserializeJson(doc, http.getStream())) {
                    float power = doc["apower"] | 0.0f;
                    bool relay = doc["output"] | false;
                    result = "{\"ok\":true,\"power\":" + String(power, 1) + ",\"gen\":\"Gen2\",\"relay\":" + (relay ? "true" : "false") + "}";
                } else {
                    result = "{\"ok\":false,\"error\":\"JSON parse error (Gen2)\"}";
                }
                http.end();
            } else {
                http.end();
                url = "http://" + ip + "/status";
                http.begin(client, url);
                code = http.GET();
                if (code == 200) {
                    JsonDocument doc;
                    if (!deserializeJson(doc, http.getStream())) {
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
                http.end();
            }
        }
        request->send(200, "application/json", result);
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
