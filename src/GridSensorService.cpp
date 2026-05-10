#include "GridSensorService.h"
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "MqttManager.h"
#include "Logger.h"

static constexpr float SENSOR_ERROR_VALUE = -99999.0f;

float GridSensorService::currentGridPower = 0.0;
float GridSensorService::currentEquip1PowerFromJsy = 0.0;
float GridSensorService::currentGridVoltage = 230.0;
bool GridSensorService::hasFreshData = false;
const Config* GridSensorService::_config = nullptr;
HardwareSerial* GridSensorService::_jsy1Serial = nullptr;
HardwareSerial* GridSensorService::_jsy2Serial = nullptr;
WiFiClient GridSensorService::_client;
HTTPClient GridSensorService::_http;

void GridSensorService::init(const Config& config) {
    _config = &config;
    _jsy1Serial = nullptr;
    _jsy2Serial = nullptr;
    _http.setConnectTimeout(2000);
    // Bug #5: cast to uint32_t before *1000 to avoid overflow above ~32 s.
    _http.setTimeout((uint32_t)_config->shelly_timeout * 1000UL);

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

bool GridSensorService::fetchGridData() {
    if (!_config) {
        hasFreshData = false; // Bug #6
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

    if (needsJsy1) jsy1_ok = readJSY(_jsy1Serial, jsy1_p1, jsy1_p2);
    if (needsJsy2) jsy2_ok = readJSY(_jsy2Serial, jsy2_p1, jsy2_p2);

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
            gridPower = MqttManager::latestMqttGridPower;
            // Bug #4: clamp upper bound on voltage too
            float v = MqttManager::latestMqttGridVoltage;
            if (v > 100.0f && v < 300.0f) {
                currentGridVoltage = v;
            }
            MqttManager::hasLatestMqttGridPower = false;
            fresh = true;
        }
    }
    // Shelly HTTP Method (Fallback)
    else {
        static uint32_t lastHttpPoll = 0;
        uint32_t now = millis();
        if (now - lastHttpPoll >= 1000) {
            gridPower = getShellyPower();
            lastHttpPoll = now;
            if (gridPower != SENSOR_ERROR_VALUE) {
                fresh = true;
            }
        }
    }

    if (fresh && !isnan(gridPower)) {
        currentGridPower = gridPower;
        hasFreshData = true;
        return true;
    }

    // Bug #6: explicitly clear stale flag on any failure path so consumers
    // can't mistake an old reading for fresh data.
    hasFreshData = false;
    return false;
}

bool GridSensorService::readJSY(HardwareSerial* serial, float& p1, float& p2) {
    if (!serial) return false;

    // JSY-MK-194G: read block starting at 0x0048 (14 registers).
    // Ch1 active power at 0x004A (u32, W = DATA/10000)
    // Ch2 active power at 0x0052 (u32, W = DATA/10000)
    uint8_t query[] = {0x01, 0x03, 0x00, 0x48, 0x00, 0x0E, 0x44, 0x18};

    // Bug #8: cap clear-buffer loop to avoid infinite spin under RX storm.
    {
        int drain = 0;
        while (serial->available() && drain < 256) { serial->read(); drain++; }
    }

    serial->write(query, 8);
    serial->flush();

    // Expected response: addr(1)+func(1)+bytecount(1)+data(28)+crc(2)=33 bytes
    const int EXPECTED_LEN = 33;
    uint8_t response[40];
    uint32_t startTime = millis();
    int idx = 0;
    bool synced = false;

    // Bug #2: resync on slave address byte 0x01 to discard preceding garbage.
    while ((millis() - startTime) < 150 && idx < EXPECTED_LEN) {
        if (serial->available()) {
            uint8_t b = serial->read();
            if (!synced) {
                if (b == 0x01) {
                    response[0] = b;
                    idx = 1;
                    synced = true;
                }
                // else discard byte
            } else {
                response[idx++] = b;
            }
        } else {
            // Bug #7: yield to avoid starving other tasks during the wait window
            vTaskDelay(1);
        }
    }

    if (idx < EXPECTED_LEN) return false;

    // Bug #1: validate CRC. Modbus RTU CRC is over all bytes except the last 2,
    // and is little-endian on the wire (low byte first).
    uint16_t crcCalc = calculateCRC(response, EXPECTED_LEN - 2);
    uint16_t crcWire = (uint16_t)response[EXPECTED_LEN - 2] | ((uint16_t)response[EXPECTED_LEN - 1] << 8);
    if (crcCalc != crcWire) {
        Logger::warn("JSY: CRC mismatch, discarding frame");
        return false;
    }

    // Parse active power channels.
    // Data payload begins at response[3]. Each register is BE 16-bit.
    const int dataBase = 3;
    auto readU32 = [&](int byteOffset) -> uint32_t {
        return ((uint32_t)response[dataBase + byteOffset] << 24)
             | ((uint32_t)response[dataBase + byteOffset + 1] << 16)
             | ((uint32_t)response[dataBase + byteOffset + 2] << 8)
             |  (uint32_t)response[dataBase + byteOffset + 3];
    };

    uint32_t p1_raw = readU32(4);   // 0x004A
    uint32_t p2_raw = readU32(20);  // 0x0052
    p1 = (float)p1_raw / 10000.0f;
    p2 = (float)p2_raw / 10000.0f;

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
        // Bug #9: seed once so successive boots produce different noise sequences.
        static bool seeded = false;
        if (!seeded) { randomSeed((uint32_t)esp_random()); seeded = true; }
        // Bug #12: wrap phase to avoid float precision drift over weeks.
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
            // Bug #3: validate the "power" key is actually present and numeric.
            // ArduinoJson's implicit conversion otherwise returns 0.0 silently
            // for a missing key, which would be treated as a real reading.
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
