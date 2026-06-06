#include "GridSensorService.h"
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "MqttManager.h"
#include "Shelly1PMManager.h"
#include "Logger.h"

static constexpr float SENSOR_ERROR_VALUE = -99999.0f;

float GridSensorService::currentEquip1PowerFromJsy = 0.0;
volatile float GridSensorService::currentGridPower = 0.0f;
float GridSensorService::currentGridVoltage = 230.0;
std::atomic<bool> GridSensorService::hasFreshData{false};
const Config* GridSensorService::_config = nullptr;
HardwareSerial* GridSensorService::_jsy1Serial = nullptr;
HardwareSerial* GridSensorService::_jsy2Serial = nullptr;
GridSensorService::JsyState GridSensorService::_jsy1State;
GridSensorService::JsyState GridSensorService::_jsy2State;
WiFiClient GridSensorService::_client;
HTTPClient GridSensorService::_http;

TaskHandle_t GridSensorService::_pollTaskHandle = nullptr;

void GridSensorService::init(const Config& config) {
    _config = &config;
    _jsy1Serial = nullptr;
    _jsy2Serial = nullptr;
    _jsy1State.state = JsyState::IDLE;
    _jsy2State.state = JsyState::IDLE;
    _http.setConnectTimeout(2000);
    _http.setTimeout((uint32_t)_config->shelly_timeout * 1000UL); // Cast before multiplying to avoid overflow above ~32s.

    if (isJsy1Active()) {
        _jsy1Serial = &Serial1;
        _jsy1Serial->begin(4800, SERIAL_8N1, config.jsy1_rx, config.jsy1_tx);
        char buf[96];
        snprintf(buf, sizeof(buf), "JSY1 initialized on UART 1 (RX:%d TX:%d)",
                 config.jsy1_rx, config.jsy1_tx);
        Logger::info(String(buf));
    }

    if (isJsy2Active()) {
        _jsy2Serial = &Serial2;
        _jsy2Serial->begin(4800, SERIAL_8N1, config.jsy2_rx, config.jsy2_tx);
        char buf[96];
        snprintf(buf, sizeof(buf), "JSY2 initialized on UART 2 (RX:%d TX:%d)",
                 config.jsy2_rx, config.jsy2_tx);
        Logger::info(String(buf));
    }

    startBackgroundPoll();
}

bool GridSensorService::isGridSourceJsy1() {
    return _config && _config->grid_measure_source == "jsy1";
}

bool GridSensorService::isGridSourceJsy2() {
    return _config && _config->grid_measure_source == "jsy2";
}

bool GridSensorService::isEquip1SourceJsy1() {
    return _config && _config->equip1_measure_source == "jsy1";
}

bool GridSensorService::isEquip1SourceJsy2() {
    return _config && _config->equip1_measure_source == "jsy2";
}

bool GridSensorService::isJsy1Active() {
    return _config && (isGridSourceJsy1() || isEquip1SourceJsy1());
}

bool GridSensorService::isJsy2Active() {
    return _config && (isGridSourceJsy2() || isEquip1SourceJsy2());
}

bool GridSensorService::isJsyActive() {
    return isJsy1Active() || isJsy2Active();
}

void GridSensorService::stopBackgroundPoll() {
    if (_pollTaskHandle != nullptr) {
        vTaskDelete(_pollTaskHandle);
        _pollTaskHandle = nullptr;
    }
}

void GridSensorService::startBackgroundPoll() {
    stopBackgroundPoll();
    xTaskCreatePinnedToCore(networkPollTask, "netPollTask", 8192, NULL, 1, &_pollTaskHandle, 0);
}

void GridSensorService::networkPollTask(void* pvParameters) {
    esp_task_wdt_add(NULL);
    while (true) {
        esp_task_wdt_reset();
        
        if (_config) {
            // 1. Grid Shelly HTTP Poll (if source is shelly and MQTT is off)
            if (_config->grid_measure_source == "shelly" && !_config->e_shelly_mqtt) {
                float p = fetchShellyHttpData();
                if (p != SENSOR_ERROR_VALUE) {
                    currentGridPower = p;
                    hasFreshData.store(true);
                }
            }

            esp_task_wdt_reset();

            // 2. Equipments Shelly HTTP Poll (if MQTT is off or stale)
            Shelly1PMManager::performBackgroundHttpUpdate();

            esp_task_wdt_reset();
        }

        vTaskDelay(pdMS_TO_TICKS(100)); // Background frequency (10Hz)
    }
}

bool GridSensorService::fetchGridData() {
    if (!_config) {
        hasFreshData.store(false);
        return false;
    }

    float gridPower = SENSOR_ERROR_VALUE;
    bool fresh = false;

    bool needsJsy1 = isGridSourceJsy1() || isEquip1SourceJsy1();
    bool needsJsy2 = isGridSourceJsy2() || isEquip1SourceJsy2();

    float jsy1_p1 = 0, jsy1_p2 = 0;
    float jsy2_p1 = 0, jsy2_p2 = 0;
    bool jsy1_ok = false;
    bool jsy2_ok = false;

    if (needsJsy1) jsy1_ok = pollJSY(_jsy1Serial, _jsy1State, jsy1_p1, jsy1_p2);
    if (needsJsy2) jsy2_ok = pollJSY(_jsy2Serial, _jsy2State, jsy2_p1, jsy2_p2);

    // Update currentEquip1PowerFromJsy
    if (isEquip1SourceJsy1() && jsy1_ok) {
        currentEquip1PowerFromJsy = (_config->jsy_equip1_channel == 2) ? jsy1_p2 : jsy1_p1;
    } else if (isEquip1SourceJsy2() && jsy2_ok) {
        currentEquip1PowerFromJsy = (_config->jsy_equip1_channel == 2) ? jsy2_p2 : jsy2_p1;
    }

    // Grid source for grid
    if (isGridSourceJsy1()) {
        if (jsy1_ok) {
            gridPower = (_config->jsy_grid_channel == 2) ? jsy1_p2 : jsy1_p1;
            fresh = true;
        }
    } else if (isGridSourceJsy2()) {
        if (jsy2_ok) {
            gridPower = (_config->jsy_grid_channel == 2) ? jsy2_p2 : jsy2_p1;
            fresh = true;
        }
    }
    // Shelly MQTT Method
    else if (_config->e_shelly_mqtt) {
        if (MqttManager::hasLatestMqttGridPower) {
            gridPower = MqttManager::latestMqttGridPower.load();
            // Clamp upper bound on voltage too
            float v = MqttManager::latestMqttGridVoltage.load();
            if (v > 100.0f && v < 300.0f) {
                currentGridVoltage = v;
            }
            MqttManager::hasLatestMqttGridPower.store(false);
            fresh = true;
        }
    }
    // Shelly HTTP Method (Handled by background task)
    else {
        if (hasFreshData.exchange(false)) {
            gridPower = currentGridPower;
            fresh = true;
        }
    }

    if (fresh && !isnan(gridPower)) {
        currentGridPower = gridPower;
        return true;
    }

    return false;
}

float GridSensorService::fetchShellyHttpData() {
    return getShellyPower();
}

bool GridSensorService::pollJSY(HardwareSerial* serial, JsyState& state, float& p1, float& p2) {
    if (!serial) return false;

    // Timeout or manual reset
    if (state.state == JsyState::WAITING && (millis() - state.queryTime > 150)) {
        state.state = JsyState::IDLE;
    }

    if (state.state == JsyState::IDLE) {
        // Drain garbage (capped to avoid infinite spin under RX storm)
        int drain = 0;
        while (serial->available() && drain < 256) { serial->read(); drain++; }
        
        uint8_t query[] = {0x01, 0x03, 0x00, 0x48, 0x00, 0x0E, 0x44, 0x18};
        serial->write(query, 8);
        serial->flush();
        state.queryTime = millis();
        state.state = JsyState::WAITING;
        return false;
    }

    // Check for response
    const int EXPECTED_LEN = 33;
    if (serial->available() < EXPECTED_LEN) return false;

    // Modbus RTU Sync: find slave addr 0x01
    while (serial->available() > 0 && serial->peek() != 0x01) {
        serial->read();
    }
    if (serial->available() < EXPECTED_LEN) return false;

    uint8_t response[40];
    serial->readBytes(response, EXPECTED_LEN);

    uint16_t crcCalc = calculateCRC(response, EXPECTED_LEN - 2);
    uint16_t crcWire = (uint16_t)response[EXPECTED_LEN - 2] | ((uint16_t)response[EXPECTED_LEN - 1] << 8);
    
    if (crcCalc != crcWire) {
        state.state = JsyState::IDLE;
        return false;
    }

    const int dataBase = 3;
    auto readU32 = [&](int byteOffset) -> uint32_t {
        return ((uint32_t)response[dataBase + byteOffset] << 24)
             | ((uint32_t)response[dataBase + byteOffset + 1] << 16)
             | ((uint32_t)response[dataBase + byteOffset + 2] << 8)
             |  (uint32_t)response[dataBase + byteOffset + 3];
    };

    p1 = (float)readU32(4) / 10000.0f;
    p2 = (float)readU32(20) / 10000.0f;

    state.state = JsyState::IDLE;
    return true;
}

uint16_t GridSensorService::calculateCRC(uint8_t *array, uint8_t len) {
    uint16_t crc = 0xFFFF;
    for (uint8_t pos = 0; pos < len; pos++) {
        crc ^= (uint16_t)array[pos];
        for (uint8_t i = 8; i != 0; i--) {
            if ((crc & 0x0001) != 0) {
                crc >>= 1;
                crc ^= 0xA001;
            } else {
                crc >>= 1;
            }
        }
    }
    return crc;
}

float GridSensorService::getShellyPower() {
    if (!_config) return SENSOR_ERROR_VALUE;

    if (_config->fake_shelly) {
        // Seed once so successive boots produce different noise sequences.
        static bool seeded = false;
        if (!seeded) { randomSeed((uint32_t)esp_random()); seeded = true; }
        // Wrap phase to avoid float precision drift over weeks.
        static float phase = 0;
        phase += 0.1f;
        if (phase > 6283.0f) phase -= 6283.0f; // ~1000 * 2pi
        float noise = ((float)esp_random() / (float)UINT32_MAX * 20.0f - 10.0f);
        return _config->export_setpoint + (sinf(phase) * 100.0f) + noise;
    }

    if (WiFi.status() != WL_CONNECTED) return SENSOR_ERROR_VALUE;

    char url[300];
    snprintf(url, sizeof(url), "http://%s/emeter/%d", _config->shelly_em_ip.c_str(), _config->shelly_em_index);
    _http.begin(_client, url);

    int httpCode = _http.GET();
    float power = SENSOR_ERROR_VALUE;

    if (httpCode == HTTP_CODE_OK) {
        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, _http.getStream());
        if (!error) {
            // Validate the "power" key is actually present and numeric — ArduinoJson's implicit conversion would otherwise return 0.0 silently for a missing key, which would be treated as a real reading.
            if (doc["power"].is<float>() || doc["power"].is<int>()) {
                power = doc["power"].as<float>();
                if (doc["voltage"].is<float>() || doc["voltage"].is<int>()) {
                    float voltage = doc["voltage"].as<float>();
                    if (voltage > 100.0f && voltage < 300.0f) {
                        currentGridVoltage = voltage;
                    }
                }
            } else {
                Logger::warn("Shelly HTTP: missing 'power' field");
            }
        }
    }

    _http.end();
    return power;
}
