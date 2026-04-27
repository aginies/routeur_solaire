#include "ActuatorManager.h"
#include "Logger.h"
#include "SafetyManager.h"
#include "GridSensorService.h"

float ActuatorManager::currentDuty = 0.0;
float ActuatorManager::equipmentPower = 0.0;
bool ActuatorManager::equipmentActive = false;
bool ActuatorManager::fanActive = false;
int ActuatorManager::fanPercent = 0;
uint32_t ActuatorManager::boostEndTime = 0;
int ActuatorManager::ssrPin = -1;

const Config* ActuatorManager::_config = nullptr;
uint32_t ActuatorManager::_lastOffTime = 0;

void ActuatorManager::init(const Config& config) {
    _config = &config;
    ssrPin = config.ssr_pin;
    
    pinMode(config.ssr_pin, OUTPUT);
    digitalWrite(config.ssr_pin, LOW);
    
    pinMode(config.relay_pin, OUTPUT);
    digitalWrite(config.relay_pin, LOW); // Relay ON (Normal Closed)
    
    if (config.e_fan) {
        ledcSetup(4, 25000, 12);
        ledcAttachPin(config.fan_pin, 4);
        ledcWrite(4, 0);
    }
}

void ActuatorManager::setDuty(float duty) {
    currentDuty = duty;
    float actualMaxPower = _config->equipment_max_power * (GridSensorService::currentGridVoltage / 230.0f) * (GridSensorService::currentGridVoltage / 230.0f);
    equipmentPower = currentDuty * actualMaxPower;
}

void ActuatorManager::openRelay() {
    if (_config) digitalWrite(_config->relay_pin, HIGH);
}

void ActuatorManager::closeRelay() {
    if (_config) digitalWrite(_config->relay_pin, LOW);
}

bool ActuatorManager::setFanSpeed(int percent, bool isTest) {
    if (!_config || !_config->e_fan) return false;
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    if (percent == fanPercent && !isTest) return true;

    int duty = (percent * 4095) / 100;
    if (isTest) {
        Serial.printf("Fan: MANUAL TEST speed: %d%% (Duty: %d/4095)\n", percent, duty);
    }
    
    ledcWrite(4, duty);
    fanPercent = percent;
    fanActive = (percent > 0);
    return true;
}

void ActuatorManager::startBoost(int minutes) {
    if (!_config) return;
    int duration = (minutes == -1) ? _config->boost_minutes : minutes;
    boostEndTime = (millis() / 1000) + (duration * 60);
    Logger::info("Solar Boost Started (" + String(duration) + " min)");
}

void ActuatorManager::cancelBoost() {
    boostEndTime = 0;
    Logger::info("Solar Boost Cancelled");
}

bool ActuatorManager::inForceWindow() {
    if (!_config || !_config->e_force_window) return false;
    time_t now;
    time(&now);
    struct tm ti;
    localtime_r(&now, &ti);
    int currMin = ti.tm_hour * 60 + ti.tm_min;
    int start = timeToMinutes(_config->force_start);
    int end = timeToMinutes(_config->force_end);
    if (start < end) return (currMin >= start && currMin < end);
    else return (currMin >= start || currMin < end);
}

int ActuatorManager::timeToMinutes(String hhmm) {
    int colonIdx = hhmm.indexOf(':');
    if (colonIdx == -1) return 0;
    return hhmm.substring(0, colonIdx).toInt() * 60 + hhmm.substring(colonIdx + 1).toInt();
}
