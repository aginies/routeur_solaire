#include "ActuatorManager.h"
#include "Logger.h"
#include "SafetyManager.h"
#include "GridSensorService.h"
#include "ControlStrategy.h"
#include <cmath>

// Bug #7: avoid hardcoding LEDC channel literal
#define FAN_LEDC_CHANNEL 4

volatile float ActuatorManager::currentDuty = 0.0;
volatile float ActuatorManager::equipmentPower = 0.0;
volatile bool ActuatorManager::equipmentActive = false;
volatile bool ActuatorManager::fanActive = false;
volatile int ActuatorManager::fanPercent = 0;
volatile uint32_t ActuatorManager::boostEndTime = 0;
int ActuatorManager::ssrPin = -1;

const Config* ActuatorManager::_config = nullptr;
uint32_t ActuatorManager::_lastOffTime = 0;
bool ActuatorManager::_initialized = false;

void ActuatorManager::init(const Config& config) {
    if (_initialized) {
        // Bug #5: re-init: if pins changed via config, the old PWM/digital pins
        // are NOT reset and the new ones not configured. Reboot is required for
        // pin changes to take effect; warn loudly so the user knows.
        if (_config && (_config->ssr_pin != config.ssr_pin
                     || _config->relay_pin != config.relay_pin
                     || _config->fan_pin != config.fan_pin
                     || _config->e_fan != config.e_fan)) {
            Logger::warn("ActuatorManager: pin/fan config changed; reboot required to apply");
        }
        _config = &config;
        return;
    }

    _config = &config;
    ssrPin = config.ssr_pin;

    pinMode(config.ssr_pin, OUTPUT);
    digitalWrite(config.ssr_pin, LOW);

    pinMode(config.relay_pin, OUTPUT);
    digitalWrite(config.relay_pin, LOW); // Relay ON (Normal Closed)

    if (config.e_fan) {
        ledcSetup(FAN_LEDC_CHANNEL, 10000, 10);
        ledcAttachPin(config.fan_pin, FAN_LEDC_CHANNEL);
        ledcWrite(FAN_LEDC_CHANNEL, 0);
    }

    _initialized = true;
}

void ActuatorManager::setDuty(float duty) {
    // Bug #1: guard against being called before init() (e.g. from MQTT command
    // arriving during early boot). Without this we deref a null _config.
    if (!_config) {
        currentDuty = duty;
        ControlStrategy::setDutyMilli((uint32_t)lroundf(duty * 1000.0f));
        equipmentPower = 0.0f;
        return;
    }
    currentDuty = duty;
    ControlStrategy::setDutyMilli((uint32_t)lroundf(duty * 1000.0f));

    // Bug #3: clamp grid voltage to a sane range. A failed sensor (returning 0
    // or a wild value) would otherwise make equipmentPower meaningless. Fall
    // back to nominal 230V when out of range.
    float v = GridSensorService::currentGridVoltage;
    if (v < 180.0f || v > 260.0f) v = 230.0f;
    float scale = (v / 230.0f) * (v / 230.0f);

    float actualMaxPower = _config->equip1_max_power * scale;
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

    int duty = (percent * 1023) / 100;
    if (isTest) {
        // Bug #4: use Logger instead of raw Serial
        char buf[80];
        snprintf(buf, sizeof(buf), "Fan: MANUAL TEST speed: %d%% (Duty: %d/1023)", percent, duty);
        Logger::info(String(buf));
    }

    ledcWrite(FAN_LEDC_CHANNEL, duty);
    fanPercent = percent;
    fanActive = (percent > 0);
    return true;
}

void ActuatorManager::startBoost(int minutes) {
    if (!_config) return;
    int duration = (minutes == -1) ? _config->boost_minutes : minutes;
    // Bug #2: clamp duration to a sane range to prevent int overflow on
    // duration*60 and to reject obvious garbage from MQTT/HTTP. Max 24h.
    if (duration < 1) duration = 1;
    if (duration > 1440) duration = 1440;
    boostEndTime = (millis() / 1000) + ((uint32_t)duration * 60UL);
    Logger::info("Solar Boost Started (" + String(duration) + " min)");
}

void ActuatorManager::cancelBoost() {
    // Bug #8: only log if a boost was actually active
    if (boostEndTime != 0) {
        boostEndTime = 0;
        Logger::info("Solar Boost Cancelled");
    }
}

bool ActuatorManager::inForceWindow() {
//    if (!_config || !_config->e_force_window) return false;
    if (!_config) return false;
    if (_config->force_equipment) return true;
    if (!_config->e_force_window) return false;

    time_t now;
    time(&now);
    struct tm ti;
    localtime_r(&now, &ti);
    if (ti.tm_year + 1900 < 2024) return false;
    int currMin = ti.tm_hour * 60 + ti.tm_min;
    int start = timeToMinutes(_config->force_start);
    int end = timeToMinutes(_config->force_end);
    if (start < end) return (currMin >= start && currMin < end);
    else return (currMin >= start || currMin < end);
}

int ActuatorManager::timeToMinutes(const String& hhmm) {
    int colonIdx = hhmm.indexOf(':');
    if (colonIdx == -1) return 0;
    // Bug #6: clamp parsed values to valid HH:MM range
    int hh = hhmm.substring(0, colonIdx).toInt();
    int mm = hhmm.substring(colonIdx + 1).toInt();
    if (hh < 0) hh = 0; if (hh > 23) hh = 23;
    if (mm < 0) mm = 0; if (mm > 59) mm = 59;
    return hh * 60 + mm;
}
