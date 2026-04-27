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
WiFiClient GridSensorService::_wifiClient;
HardwareSerial* GridSensorService::_jsySerial = nullptr;

void GridSensorService::init(const Config& config) {
    _config = &config;

    if (config.e_jsy) {
        if (config.jsy_uart_id == 1) _jsySerial = &Serial1;
        else if (config.jsy_uart_id == 2) _jsySerial = &Serial2;
        
        if (_jsySerial) {
            _jsySerial->begin(4800, SERIAL_8N1, config.jsy_rx, config.jsy_tx);
            Logger::info("JSY-MK-194 initialized on UART " + String(config.jsy_uart_id) + " (RX:" + String(config.jsy_rx) + " TX:" + String(config.jsy_tx) + ")");
        }
    }
}

bool GridSensorService::isJsyActive() {
    return _config && _config->e_jsy;
}

bool GridSensorService::fetchGridData() {
    if (!_config) return false;
    
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
            if (MqttManager::latestMqttGridVoltage > 100.0) {
                currentGridVoltage = MqttManager::latestMqttGridVoltage;
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
    
    return false;
}

float GridSensorService::readJSY() {
    if (!_jsySerial) return SENSOR_ERROR_VALUE;

    // Modbus RTU Request for registers 0x0000 (Voltage, Current, Power, Energy...)
    // Address 01, Function 03, Start 0000, Count 0006
    uint8_t query[] = {0x01, 0x03, 0x00, 0x00, 0x00, 0x06, 0xC5, 0xC8};
    
    // Clear buffer
    while (_jsySerial->available()) _jsySerial->read();
    
    _jsySerial->write(query, 8);
    _jsySerial->flush();

    uint8_t response[25]; // 5 (header) + 12 (data) + 2 (crc) = 19 bytes expected
    uint32_t startTime = millis();
    int idx = 0;
    
    while (millis() - startTime < 150 && idx < 19) {
        if (_jsySerial->available()) {
            response[idx++] = _jsySerial->read();
        }
    }

    if (idx < 19) return SENSOR_ERROR_VALUE;
    
    // Simple CRC check (optional but recommended)
    // uint16_t crc = calculateCRC(response, 17);
    // if (response[17] != (crc & 0xFF) || response[18] != (crc >> 8)) return -99999.0;

    // Parsing JSY-MK-194 response (Big Endian)
    // Voltage: 0.1V steps, indices 3,4
    // Current: 0.01A steps, indices 5,6
    // Power: 1W steps, indices 7,8 (Active Power)
    // For MK-194, registers are 16-bit integers
    
    uint16_t v_raw = (response[3] << 8) | response[4];
    uint16_t p_raw = (response[7] << 8) | response[8];
    uint16_t p_sign = (response[9] << 8) | response[10]; // Combined with 0x0100 for negative? 
    // Actually, on many JSY-MK-194, register 0x0003 is signed power or there is a sign bit.
    
    currentGridVoltage = v_raw / 10.0f;
    float power = (float)p_raw;
    
    // Handle Sign (JSY uses a separate register or MSB for direction depending on model)
    // Standard MK-194: Register 0x0003 is Power, 0x0004 is Power Direction (0=Import, 1=Export)
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
        static float phase = 0;
        phase += 0.1;
        float noise = (random(100) - 50) / 10.0;
        return _config->export_setpoint + (sin(phase) * 100.0) + noise;
    }

    if (WiFi.status() != WL_CONNECTED) return -99999.0;

    HTTPClient http;
    http.setConnectTimeout(2000);
    http.setTimeout(_config->shelly_timeout * 1000);
    
    String url = "http://" + _config->shelly_em_ip + "/emeter/0";
    http.begin(_wifiClient, url);
    
    int httpCode = http.GET();
    float power = SENSOR_ERROR_VALUE;

    if (httpCode == HTTP_CODE_OK) {
        String payload = http.getString();
        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, payload);
        if (!error) {
            power = doc["power"];
            float voltage = doc["voltage"];
            if (voltage > 100.0 && voltage < 300.0) {
                currentGridVoltage = voltage;
            }
        }
    }
    
    http.end(); 
    return power;
}
