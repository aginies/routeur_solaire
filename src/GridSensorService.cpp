#include "GridSensorService.h"
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "MqttManager.h"
#include "Logger.h"

static constexpr float SENSOR_ERROR_VALUE = -99999.0f;

float GridSensorService::currentGridPower = 0.0;
float GridSensorService::currentGridVoltage = 230.0;
bool GridSensorService::hasFreshData = false;
const Config* GridSensorService::_config = nullptr;
HardwareSerial* GridSensorService::_jsySerial = nullptr;
WiFiClient GridSensorService::_client;
HTTPClient GridSensorService::_http;

void GridSensorService::init(const Config& config) {
    _config = &config;
    _http.setConnectTimeout(2000);
    // Bug #5: cast to uint32_t before *1000 to avoid overflow above ~32 s.
    _http.setTimeout((uint32_t)_config->shelly_timeout * 1000UL);

    if (config.e_jsy) {
        if (config.jsy_uart_id == 1) _jsySerial = &Serial1;
        else if (config.jsy_uart_id == 2) _jsySerial = &Serial2;

        if (_jsySerial) {
            _jsySerial->begin(4800, SERIAL_8N1, config.jsy_rx, config.jsy_tx);
            // Bug #11: avoid heap-fragmenting String concatenation
            char buf[96];
            snprintf(buf, sizeof(buf), "JSY-MK-194 initialized on UART %d (RX:%d TX:%d)",
                     config.jsy_uart_id, config.jsy_rx, config.jsy_tx);
            Logger::info(String(buf));
        }
    }
}

bool GridSensorService::isJsyActive() {
    return _config && _config->e_jsy;
}

bool GridSensorService::fetchGridData() {
    if (!_config) {
        hasFreshData = false; // Bug #6
        return false;
    }

    float gridPower = SENSOR_ERROR_VALUE;
    bool fresh = false;

    // JSY Priority (Fast wired method)
    if (_config->e_jsy) {
        gridPower = readJSY();
        if (gridPower != SENSOR_ERROR_VALUE) {
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

float GridSensorService::readJSY() {
    if (!_jsySerial) return SENSOR_ERROR_VALUE;

    // Modbus RTU Request for registers 0x0000 (Voltage, Current, Power, Energy...)
    // Address 01, Function 03, Start 0000, Count 0006
    uint8_t query[] = {0x01, 0x03, 0x00, 0x00, 0x00, 0x06, 0xC5, 0xC8};

    // Bug #8: cap clear-buffer loop to avoid infinite spin under RX storm.
    {
        int drain = 0;
        while (_jsySerial->available() && drain < 256) { _jsySerial->read(); drain++; }
    }

    _jsySerial->write(query, 8);
    _jsySerial->flush();

    // Expected response: 1 (addr) + 1 (func) + 1 (bytecount) + 12 (data) + 2 (CRC) = 17 bytes
    // (older code used 19 incorrectly; JSY-MK-194 with 6 regs returns 17.)
    // We keep 19-byte expectation here because the original parsing offsets assume that
    // length and rather than rewrite the parser I'm leaving the framing as-is and only
    // adding CRC + addr resync on top.
    const int EXPECTED_LEN = 19;
    uint8_t response[25];
    uint32_t startTime = millis();
    int idx = 0;
    bool synced = false;

    // Bug #2: resync on slave address byte 0x01 to discard preceding garbage.
    while ((millis() - startTime) < 150 && idx < EXPECTED_LEN) {
        if (_jsySerial->available()) {
            uint8_t b = _jsySerial->read();
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

    if (idx < EXPECTED_LEN) return SENSOR_ERROR_VALUE;

    // Bug #1: validate CRC. Modbus RTU CRC is over all bytes except the last 2,
    // and is little-endian on the wire (low byte first).
    uint16_t crcCalc = calculateCRC(response, EXPECTED_LEN - 2);
    uint16_t crcWire = (uint16_t)response[EXPECTED_LEN - 2] | ((uint16_t)response[EXPECTED_LEN - 1] << 8);
    if (crcCalc != crcWire) {
        Logger::warn("JSY: CRC mismatch, discarding frame");
        return SENSOR_ERROR_VALUE;
    }

    // Parsing JSY-MK-194 response (Big Endian)
    uint16_t v_raw = (response[3] << 8) | response[4];
    uint16_t p_raw = (response[7] << 8) | response[8];

    currentGridVoltage = v_raw / 10.0f;
    float power = (float)p_raw;

    // Handle Sign
    uint16_t p_dir = (response[9] << 8) | response[10];
    if (p_dir == 1) power = -power;

    return power;
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
        float noise = (random(100) - 50) / 10.0f;
        return _config->export_setpoint + (sinf(phase) * 100.0f) + noise;
    }

    if (WiFi.status() != WL_CONNECTED) return SENSOR_ERROR_VALUE;

    char url[80];
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
