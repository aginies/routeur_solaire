#include "WebManager.h"
#include "ConfigManager.h"
#include "Logger.h"
#include "NetworkManager.h"
#include "MqttManager.h"
#include "GridSensorService.h"
#include "StatsManager.h"
#include "ActuatorManager.h"
#include "ControlStrategy.h"
#include "TemperatureManager.h"
#include "SafetyManager.h"
#include "HistoryBuffer.h"
#include "WeatherManager.h"
#include "Equipment2Manager.h"
#include "Shelly1PMManager.h"
#include "Utils.h"
#include "PinCapabilities.h"
#include <HTTPClient.h>
#include <LittleFS.h>
#include <Update.h>
#include <ArduinoJson.h>
AsyncWebServer WebManager::_server(80);
WiFiClient WebManager::_client;
HTTPClient WebManager::_http;
SemaphoreHandle_t WebManager::_httpMutex = nullptr;
const Config* WebManager::_config = nullptr;
bool WebManager::_rebootRequested = false;

void WebManager::init(const Config& config) {
    _config = &config;
    _http.setConnectTimeout(1000);
    _http.setTimeout(2000);
    if (!_httpMutex) _httpMutex = xSemaphoreCreateMutex(); // Bug #6
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

    // Bug #5: helpers to clamp/validate user-supplied values before persisting them.
    auto clampInt = [](int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); };
    auto clampFloat = [](float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); };
    auto setRolePin = [&](int& dest, int v, PinRole role) {
        if (isPinValidForRole(v, role)) {
            dest = v;
        } else {
            Logger::warn(String("Rejected ") + pinRoleName(role) + "=" + v + " (" + pinValidationReason(v, role) + ")");
        }
    };

    // System
    if (has("NAME")) cfg.name = get("NAME").substring(0, 64);
    if (has("TIMEZONE")) cfg.timezone = get("TIMEZONE").substring(0, 64);
    if (has("CPU_FREQ")) {
        int f = get("CPU_FREQ").toInt();
        if (f == 80 || f == 160 || f == 240) cfg.cpu_freq = f;
        else Logger::warn("Rejected CPU_FREQ " + String(f));
    }
    if (has("MAX_ESP32_TEMP")) cfg.max_esp32_temp = clampFloat(get("MAX_ESP32_TEMP").toFloat(), 40.0f, 110.0f);

    // WiFi
    if (has("E_WIFI")) cfg.e_wifi = (get("E_WIFI") == "True");
    if (has("WIFI_SSID")) cfg.wifi_ssid = get("WIFI_SSID").substring(0, 32);
    if (has("WIFI_PASSWORD")) cfg.wifi_password = get("WIFI_PASSWORD").substring(0, 64);
    if (has("WIFI_STATIC_IP")) cfg.wifi_static_ip = get("WIFI_STATIC_IP").substring(0, 45);
    if (has("WIFI_SUBNET")) cfg.wifi_subnet = get("WIFI_SUBNET").substring(0, 45);
    if (has("WIFI_GATEWAY")) cfg.wifi_gateway = get("WIFI_GATEWAY").substring(0, 45);
    if (has("WIFI_DNS")) cfg.wifi_dns = get("WIFI_DNS").substring(0, 45);

    // Shelly / Grid
    if (has("SHELLY_EM_IP")) cfg.shelly_em_ip = get("SHELLY_EM_IP").substring(0, 45);
    if (has("SHELLY_EM_INDEX")) cfg.shelly_em_index = clampInt(get("SHELLY_EM_INDEX").toInt(), 0, 7);
    if (has("E_SHELLY_MQTT")) cfg.e_shelly_mqtt = (get("E_SHELLY_MQTT") == "True");
    if (has("SHELLY_MQTT_TOPIC")) cfg.shelly_mqtt_topic = get("SHELLY_MQTT_TOPIC").substring(0, 128);
    if (has("GRID_MEASURE_SOURCE")) {
        String s = get("GRID_MEASURE_SOURCE");
        if (s == "jsy" || s == "shelly") cfg.grid_measure_source = s;
    }
    // Bug #15: these four fields are in SECONDS (code multiplies by 1000 before
    // comparing against millis()/setTimeout()). The previous clamp ranges
    // assumed milliseconds and silently rewrote valid user values on every save
    // (e.g. shelly_timeout=2 -> 100, meaning a 100-second HTTP timeout!).
    if (has("POLL_INTERVAL")) cfg.poll_interval = clampInt(get("POLL_INTERVAL").toInt(), 1, 3600);
    if (has("SHELLY_TIMEOUT")) cfg.shelly_timeout = clampInt(get("SHELLY_TIMEOUT").toInt(), 1, 60);
    if (has("SAFETY_TIMEOUT")) cfg.safety_timeout = clampInt(get("SAFETY_TIMEOUT").toInt(), 10, 3600);
    if (has("FAKE_SHELLY")) cfg.fake_shelly = (get("FAKE_SHELLY") == "True");

    // Equipment 1
    if (has("EQUIP1_NAME")) cfg.equip1_name = get("EQUIP1_NAME").substring(0, 64);
    if (has("EQUIP1_MAX_POWER")) cfg.equip1_max_power = clampFloat(get("EQUIP1_MAX_POWER").toFloat(), 0.0f, 20000.0f);
    if (has("E_EQUIP1")) cfg.e_equip1 = (get("E_EQUIP1") == "True");
    if (has("EQUIP1_SHELLY_IP")) cfg.equip1_shelly_ip = get("EQUIP1_SHELLY_IP").substring(0, 45);
    if (has("EQUIP1_SHELLY_INDEX")) cfg.equip1_shelly_index = clampInt(get("EQUIP1_SHELLY_INDEX").toInt(), 0, 7);
    if (has("E_EQUIP1_MQTT")) cfg.e_equip1_mqtt = (get("E_EQUIP1_MQTT") == "True");
    if (has("EQUIP1_MQTT_TOPIC")) cfg.equip1_mqtt_topic = get("EQUIP1_MQTT_TOPIC").substring(0, 128);
    if (has("EQUIP1_MEASURE_SOURCE")) {
        String s = get("EQUIP1_MEASURE_SOURCE");
        if (s == "shelly" || s == "jsy") cfg.equip1_measure_source = s;
    }

    // Equipment 2
    if (has("E_EQUIP2")) cfg.e_equip2 = (get("E_EQUIP2") == "True");
    if (has("EQUIP2_NAME")) cfg.equip2_name = get("EQUIP2_NAME").substring(0, 64);
    if (has("EQUIP2_IP")) cfg.equip2_shelly_ip = get("EQUIP2_IP").substring(0, 45);
    if (has("EQUIP2_SHELLY_INDEX")) cfg.equip2_shelly_index = clampInt(get("EQUIP2_SHELLY_INDEX").toInt(), 0, 7);
    if (has("E_EQUIP2_MQTT")) cfg.e_equip2_mqtt = (get("E_EQUIP2_MQTT") == "True");
    if (has("EQUIP2_MQTT_TOPIC")) cfg.equip2_mqtt_topic = get("EQUIP2_MQTT_TOPIC").substring(0, 128);
    if (has("EQUIP2_POWER")) cfg.equip2_max_power = clampFloat(get("EQUIP2_POWER").toFloat(), 0.0f, 20000.0f);
    if (has("EQUIP2_PRIO")) cfg.equip2_priority = clampInt(get("EQUIP2_PRIO").toInt(), 0, 10);
    if (has("EQUIP2_MIN_TIME")) cfg.equip2_min_on_time = clampInt(get("EQUIP2_MIN_TIME").toInt(), 0, 86400);

    // Strategy / PID
    if (has("DELTA")) cfg.delta = clampFloat(get("DELTA").toFloat(), 0.0f, 5000.0f);
    if (has("DELTANEG")) cfg.deltaneg = clampFloat(get("DELTANEG").toFloat(), -5000.0f, 5000.0f);
    // Bug #15b: compensation is used as a percentage (default 50). Old upper
    // bound 10 silently clipped any sensible value down to 10.
    if (has("COMPENSATION")) cfg.compensation = clampFloat(get("COMPENSATION").toFloat(), 1.0f, 100.0f);
    if (has("DYNAMIC_THRESHOLD_W")) cfg.dynamic_threshold_w = clampFloat(get("DYNAMIC_THRESHOLD_W").toFloat(), 0.0f, 5000.0f);
    if (has("EXPORT_SETPOINT")) cfg.export_setpoint = clampFloat(get("EXPORT_SETPOINT").toFloat(), -5000.0f, 5000.0f);
    if (has("MAX_DUTY_PERCENT")) cfg.max_duty_percent = clampFloat(get("MAX_DUTY_PERCENT").toFloat(), 0.0f, 100.0f);

    // Hardware
    if (has("SSR_PIN")) setRolePin(cfg.ssr_pin, get("SSR_PIN").toInt(), PinRole::SSR);
    if (has("RELAY_PIN")) setRolePin(cfg.relay_pin, get("RELAY_PIN").toInt(), PinRole::RELAY);
    if (has("DS18B20_PIN")) setRolePin(cfg.ds18b20_pin, get("DS18B20_PIN").toInt(), PinRole::DS18B20);
    if (has("I_LED_PIN")) setRolePin(cfg.internal_led_pin, get("I_LED_PIN").toInt(), PinRole::INTERNAL_LED);
    if (has("FAN_PIN")) setRolePin(cfg.fan_pin, get("FAN_PIN").toInt(), PinRole::FAN_PWM);
    if (has("FAN_TEMP_OFFSET")) cfg.fan_temp_offset = clampInt(get("FAN_TEMP_OFFSET").toInt(), -50, 50);
    if (has("E_FAN")) cfg.e_fan = (get("E_FAN") == "True");
    if (has("E_SSR_TEMP")) cfg.e_ssr_temp = (get("E_SSR_TEMP") == "True");
    if (has("SSR_MAX_TEMP")) cfg.ssr_max_temp = clampFloat(get("SSR_MAX_TEMP").toFloat(), 30.0f, 150.0f);
    if (has("ZX_PIN")) setRolePin(cfg.zx_pin, get("ZX_PIN").toInt(), PinRole::ZX_INPUT);
    if (has("CONTROL_MODE")) cfg.control_mode = get("CONTROL_MODE").substring(0, 32);
    if (has("JSY_UART_ID")) cfg.jsy_uart_id = clampInt(get("JSY_UART_ID").toInt(), 1, 2);
    if (has("JSY_GRID_CHANNEL")) cfg.jsy_grid_channel = clampInt(get("JSY_GRID_CHANNEL").toInt(), 1, 2);
    if (has("JSY_EQUIP1_CHANNEL")) cfg.jsy_equip1_channel = clampInt(get("JSY_EQUIP1_CHANNEL").toInt(), 1, 2);
    if (has("JSY_TX")) setRolePin(cfg.jsy_tx, get("JSY_TX").toInt(), PinRole::JSY_TX);
    if (has("JSY_RX")) setRolePin(cfg.jsy_rx, get("JSY_RX").toInt(), PinRole::JSY_RX);

    // Force / Night
    if (has("BOOST_MINUTES")) cfg.boost_minutes = get("BOOST_MINUTES").toInt();
    if (has("FORCE_EQUIPMENT")) cfg.force_equipment = (get("FORCE_EQUIPMENT") == "True");
    if (has("E_FORCE_WINDOW")) cfg.e_force_window = (get("E_FORCE_WINDOW") == "True");
    if (has("FORCE_START")) cfg.force_start = get("FORCE_START").substring(0, 8);
    if (has("FORCE_END")) cfg.force_end = get("FORCE_END").substring(0, 8);
    if (has("NIGHT_POLL_INTERVAL")) cfg.night_poll_interval = clampInt(get("NIGHT_POLL_INTERVAL").toInt(), 10, 3600);

    // MQTT
    if (has("E_MQTT")) cfg.e_mqtt = (get("E_MQTT") == "True");
    if (has("MQTT_IP")) cfg.mqtt_ip = get("MQTT_IP").substring(0, 64);
    if (has("MQTT_PORT")) cfg.mqtt_port = clampInt(get("MQTT_PORT").toInt(), 1, 65535);
    if (has("MQTT_USER")) cfg.mqtt_user = get("MQTT_USER").substring(0, 64);
    if (has("MQTT_PASSWORD")) cfg.mqtt_password = get("MQTT_PASSWORD").substring(0, 128);
    if (has("MQTT_NAME")) cfg.mqtt_name = get("MQTT_NAME").substring(0, 64);

    // Weather
    if (has("E_WEATHER")) cfg.e_weather = (get("E_WEATHER") == "True");
    if (has("WEATHER_LAT")) cfg.weather_lat = get("WEATHER_LAT").substring(0, 16);
    if (has("WEATHER_LON")) cfg.weather_lon = get("WEATHER_LON").substring(0, 16);
    if (has("WEATHER_THRESH")) cfg.weather_cloud_threshold = clampInt(get("WEATHER_THRESH").toInt(), 0, 100);
    if (has("SOLAR_PANEL_POWER")) cfg.solar_panel_power = clampInt(get("SOLAR_PANEL_POWER").toInt(), 0, 50000);
    if (has("SOLAR_PANEL_AZIMUTH")) cfg.solar_panel_azimuth = clampInt(get("SOLAR_PANEL_AZIMUTH").toInt(), 0, 360);
    if (has("SOLAR_PANEL_TILT")) cfg.solar_panel_tilt = clampInt(get("SOLAR_PANEL_TILT").toInt(), 0, 90);
    if (has("SOLAR_LOSS_FACTOR")) cfg.solar_loss_factor = clampInt(get("SOLAR_LOSS_FACTOR").toInt(), 0, 90);

    // Web
    if (has("WEB_USER")) cfg.web_user = get("WEB_USER").substring(0, 64);
    if (has("WEB_PASSWORD")) cfg.web_password = get("WEB_PASSWORD").substring(0, 64);
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
    _server.on("/style.css", HTTP_GET, [](AsyncWebServerRequest *request) {
        AsyncWebServerResponse *response = request->beginResponse(LittleFS, "/style.css.gz", "text/css");
        response->addHeader("Content-Encoding", "gzip");
        request->send(response);
    });
    _server.on("/main.js", HTTP_GET, [](AsyncWebServerRequest *request) {
        AsyncWebServerResponse *response = request->beginResponse(LittleFS, "/main.js.gz", "application/javascript");
        response->addHeader("Content-Encoding", "gzip");
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
    _server.on("/set_vacation", HTTP_GET, [authRequired](AsyncWebServerRequest *request) {
        if (!authRequired(request)) return;
        int days = request->hasParam("days") ? request->getParam("days")->value().toInt() : 0;
        if (days > 0) {
            time_t now;
            time(&now);
            uint32_t until = (uint32_t)now + (days * 86400UL);
            
            // Update live config memory so it takes effect immediately
            Config* liveCfg = const_cast<Config*>(_config);
            liveCfg->vacation_until = until;
            
            // Save to persistent storage
            ConfigManager::save(*liveCfg);
            
            Logger::info("Vacation mode started for " + String(days) + " days");
            request->send(200, "text/plain", "Vacances configurées");
        } else {
            request->send(400, "text/plain", "Nombre de jours invalide");
        }
    });

    _server.on("/cancel_vacation", HTTP_GET, [authRequired](AsyncWebServerRequest *request) {
        if (!authRequired(request)) return;
        
        Config* liveCfg = const_cast<Config*>(_config);
        liveCfg->vacation_until = 0;
        ConfigManager::save(*liveCfg);
        
        Logger::info("Vacation mode cancelled manually");
        request->send(200, "text/plain", "Vacances annulées");
    });

    _server.on("/get_stats", HTTP_GET, [authRequired](AsyncWebServerRequest *request) {
        if (!authRequired(request)) return;
        StatsManager::streamStatsJson(request);
    });

    _server.on("/import_stats", HTTP_POST, [authRequired](AsyncWebServerRequest *request) {
        if (!authRequired(request)) return;
        // Bug #4: report actual upload outcome via status code stored in _tempObject
        // (heap-allocated int* so AsyncWebServer can free() it).
        int status = request->_tempObject ? *(int*)request->_tempObject : -1;
        switch (status) {
            case 0: // ok
                request->send(200, "text/plain", "Importation réussie. Redémarrage...");
                _rebootRequested = true;
                break;
            case 1: // too large
                request->send(413, "text/plain", "Fichier trop volumineux");
                break;
            case 2: // write/open failed
                request->send(500, "text/plain", "Erreur d'écriture");
                break;
            default:
                request->send(400, "text/plain", "Aucun fichier reçu");
        }
        // Bug fix: free the heap-allocated status object
        if (request->_tempObject) {
            free(request->_tempObject);
            request->_tempObject = nullptr;
        }
    }, [authRequired](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
        if (!authRequired(request)) return;
        // Bug #3: state is static (AsyncWebServer serializes upload chunks per server instance)
        // but we now write to a tmp file and atomically rename only on success, so a
        // partial/aborted upload no longer corrupts the existing stats.json.
        static File uploadFile;
        static size_t uploadedBytes = 0;
        static int uploadStatus = -1; // -1 unknown, 0 ok, 1 toolarge, 2 writefail
        static constexpr size_t MAX_STATS_UPLOAD_BYTES = 200 * 1024;

        // Helper to publish status to the request (for the outer handler).
        auto setStatus = [&](int s) {
            uploadStatus = s;
            if (!request->_tempObject) {
                request->_tempObject = malloc(sizeof(int));
            }
            if (request->_tempObject) *(int*)request->_tempObject = s;
        };

        if (!index) {
            uploadedBytes = 0;
            uploadStatus = -1;
            if (uploadFile) uploadFile.close();
            Logger::info("Importing stats: " + filename);
            uploadFile = LittleFS.open("/stats_upload.json", "w");
            if (!uploadFile) {
                Logger::error("Stats upload: cannot open /stats_upload.json");
                setStatus(2);
                return;
            }
        }

        if (!uploadFile || uploadStatus == 1 || uploadStatus == 2) return; // failed earlier, drop

        uploadedBytes += len;
        if (uploadedBytes > MAX_STATS_UPLOAD_BYTES) {
            uploadFile.close();
            LittleFS.remove("/stats_upload.json");
            Logger::error("Stats upload rejected: file too large");
            setStatus(1);
            return;
        }

        if (len) {
            size_t written = uploadFile.write(data, len);
            if (written != len) {
                uploadFile.close();
                LittleFS.remove("/stats_upload.json");
                Logger::error("Stats upload write failed");
                setStatus(2);
                return;
            }
        }

        if (final) {
            uploadFile.close();
            // Atomic replace of stats.json
            LittleFS.remove("/stats.json");
            if (LittleFS.rename("/stats_upload.json", "/stats.json")) {
                Logger::info("Stats upload complete (" + String(uploadedBytes) + " bytes)");
                setStatus(0);
            } else {
                Logger::error("Stats upload: rename failed");
                LittleFS.remove("/stats_upload.json");
                setStatus(2);
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

    _server.on("/web_dev", HTTP_GET, [authRequired](AsyncWebServerRequest *request) {
        if (!authRequired(request)) return;
        AsyncWebServerResponse *response = request->beginResponse(LittleFS, "/web_dev.html.gz", "text/html");
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
            if (ConfigManager::save(newCfg)) {
                // Bug #16: schedule is loaded once at boot; reboot so the change takes effect
                request->send(200, "text/plain", "OK");
                _rebootRequested = true;
            } else {
                request->send(500);
            }
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
        doc["grid_measure_source"] = _config->grid_measure_source;
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
        doc["equip1_measure_source"] = _config->equip1_measure_source;
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
        doc["jsy_uart_id"] = _config->jsy_uart_id;
        doc["jsy_grid_channel"] = _config->jsy_grid_channel;
        doc["jsy_equip1_channel"] = _config->jsy_equip1_channel;
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
        doc["solar_panel_power"] = _config->solar_panel_power;
        doc["solar_panel_azimuth"] = _config->solar_panel_azimuth;
        doc["solar_panel_tilt"] = _config->solar_panel_tilt;
        doc["solar_loss_factor"] = _config->solar_loss_factor;
        doc["boost_minutes"] = _config->boost_minutes;
        doc["vacation_until"] = _config->vacation_until;
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

    _server.on("/update", HTTP_POST, [authRequired](AsyncWebServerRequest *request) {
        if (!authRequired(request)) return;
        // Bug #2: report actual outcome (begin failed, write failed, end failed all surface as hasError)
        bool ok = !Update.hasError();
        AsyncWebServerResponse *response = request->beginResponse(200, "text/plain",
            ok ? "OK" : (String("FAIL: ") + Update.errorString()));
        response->addHeader("Connection", "close");
        request->send(response);
        if (ok) _rebootRequested = true;
    }, [authRequired](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
        if (!authRequired(request)) {
            // Abort any in-progress flash if an unauthenticated upload reaches the data handler
            if (Update.isRunning()) Update.abort();
            return;
        }
        if (!index) {
            int command = (filename.indexOf("spiffs") > -1 || filename.indexOf("littlefs") > -1) ? U_SPIFFS : U_FLASH;
            if (!Update.begin(UPDATE_SIZE_UNKNOWN, command)) {
                Logger::error("OTA begin failed: " + String(Update.errorString()));
                return;
            }
        }
        // Bug #17: validate write count and abort on partial write
        if (Update.isRunning() && len) {
            size_t written = Update.write(data, len);
            if (written != len) {
                Logger::error("OTA write failed: " + String(Update.errorString()));
                Update.abort();
                return;
            }
        }
        // Bug #2: validate Update.end() result; do NOT reboot on failure
        if (Update.isRunning() && final) {
            if (!Update.end(true)) {
                Logger::error("OTA end failed: " + String(Update.errorString()));
                // Update.hasError() will be true so outer handler returns FAIL.
            } else {
                Logger::info("OTA upload complete: " + String(index + len) + " bytes");
            }
        }
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
        // Bug #6/#7: serialize concurrent uses of the shared _http object and reject
        // overlapping requests immediately instead of stalling the async event loop.
        if (!_httpMutex || xSemaphoreTake(_httpMutex, 0) != pdTRUE) {
            request->send(503, "application/json", "{\"ok\":false,\"error\":\"Test déjà en cours\"}");
            return;
        }

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
            xSemaphoreGive(_httpMutex);
            request->send(400, "application/json", "{\"ok\":false,\"error\":\"Invalid target\"}");
            return;
        }

        if (ip.length() == 0) {
            xSemaphoreGive(_httpMutex);
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
                        // Bug #15: ArduinoJson v7 deprecates containsKey()
                        if (doc["meters"].is<JsonArray>()) power = doc["meters"][index]["power"] | 0.0f;
                        else if (doc["emeters"].is<JsonArray>()) power = doc["emeters"][index]["power"] | 0.0f;
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
        xSemaphoreGive(_httpMutex);
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
    uint32_t nowSec = millis() / 1000;
    int32_t remaining = (int32_t)(ActuatorManager::boostEndTime - nowSec);
    doc["boost_remaining"] = remaining > 0 ? (remaining / 60) : 0;
    doc["boost_end_time"] = ActuatorManager::boostEndTime;
    doc["force_mode"] = (SafetyManager::currentState == SystemState::STATE_BOOST);
    doc["emergency_mode"] = (SafetyManager::currentState == SystemState::STATE_EMERGENCY_FAULT);
    doc["emergency_reason"] = SafetyManager::emergencyReason;
    // Bug #14: ternary mixing float and JsonVariant is ill-formed; assign explicitly.
    if (TemperatureManager::currentSsrTemp > -100.0) {
        doc["ssr_temp"] = (float)TemperatureManager::currentSsrTemp;
    } else {
        doc["ssr_temp"] = nullptr; // emit JSON null
    }
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

    if (_config->grid_measure_source == "jsy") {
        doc["grid_source"] = "JSY";
    } else {
        doc["grid_source"] = _config->e_shelly_mqtt ? "MQTT" : "HTTP";
    }
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
    doc["equip2_max_power"] = _config->equip2_max_power;
    doc["night_mode"] = SolarMonitor::isNight(Utils::getCurrentMinutes());

    doc["control_mode"] = _config->control_mode;
    doc["is_phase_mode"] = ControlStrategy::isPhaseModeActive();
    doc["is_trame_mode"] = ControlStrategy::isTrameModeActive();

    JsonObject ac = doc["ac"].to<JsonObject>();
    uint32_t zxCounter = ControlStrategy::getZxCounter();
    ac["zx_counter"] = zxCounter;
    ac["zx_last_us"] = ControlStrategy::getLastZxTimeUs();
    ac["half_cycle_parity"] = (zxCounter % 2U) ? "odd" : "even";
    ac["full_cycle_index"] = ControlStrategy::getCurrentFullCycleIndex();
    ac["grid_hz_est"] = ControlStrategy::getEstimatedGridHz();

    JsonObject trame = doc["trame"].to<JsonObject>();
    trame["enabled"] = ControlStrategy::isTrameModeActive();
    trame["fire_full_cycle"] = ControlStrategy::getTrameFireFullCycle();
    trame["decision_on_odd_cross"] = true;
    trame["decision_age_ms"] = ControlStrategy::getTrameDecisionAgeMs();

    JsonObject phase = doc["phase"].to<JsonObject>();
    phase["enabled"] = ControlStrategy::isPhaseModeActive();
    phase["timer_arm_pending"] = ControlStrategy::isPhaseTimerArmPending();
    phase["last_requested_wait_us"] = ControlStrategy::getPhaseLastRequestedWaitUs();

    JsonObject pinValidation = doc["pin_validation"].to<JsonObject>();
    bool allPinsValid = true;
    auto addPinValidation = [&](const char* key, int pin, PinRole role) {
        JsonObject p = pinValidation[key].to<JsonObject>();
        bool valid = isPinValidForRole(pin, role);
        p["pin"] = pin;
        p["valid"] = valid;
        if (!valid) {
            p["reason"] = pinValidationReason(pin, role);
            allPinsValid = false;
        }
    };
    addPinValidation("ssr_pin", _config->ssr_pin, PinRole::SSR);
    addPinValidation("relay_pin", _config->relay_pin, PinRole::RELAY);
    addPinValidation("fan_pin", _config->fan_pin, PinRole::FAN_PWM);
    addPinValidation("zx_pin", _config->zx_pin, PinRole::ZX_INPUT);
    addPinValidation("ds18b20_pin", _config->ds18b20_pin, PinRole::DS18B20);
    addPinValidation("internal_led_pin", _config->internal_led_pin, PinRole::INTERNAL_LED);
    addPinValidation("jsy_tx", _config->jsy_tx, PinRole::JSY_TX);
    addPinValidation("jsy_rx", _config->jsy_rx, PinRole::JSY_RX);
    doc["pin_validation_ok"] = allPinsValid;
    
    time_t t_now_epoch;
    time(&t_now_epoch);
    doc["vacation_until"] = _config->vacation_until;
    doc["is_vacation"] = (_config->vacation_until > (uint32_t)t_now_epoch);

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
        doc["weather_time_factor"] = WeatherManager::getTimeFactor();
        doc["weather_expected_power"] = WeatherManager::getExpectedSolarPower();
        doc["solar_panel_power"] = _config->solar_panel_power;
        doc["solar_panel_azimuth"] = _config->solar_panel_azimuth;
        doc["solar_panel_tilt"] = _config->solar_panel_tilt;
        doc["solar_loss_factor"] = _config->solar_loss_factor;
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
