#include "LcdManager.h"
#include "Logger.h"
#include "GridSensorService.h"
#include "ActuatorManager.h"
#include <Wire.h>
#include "LiquidCrystal_PCF8574.h"

bool LcdManager::_initialized = false;
char LcdManager::_scrollBuf[48] = {0};
int LcdManager::_scrollPos = 0;
uint32_t LcdManager::_lastScroll = 0;

static LiquidCrystal_PCF8574* _lcd = nullptr;

void LcdManager::init(const String& ssid, const String& ip, byte i2cAddr, int sdaPin, int sclPin, uint8_t cols, uint8_t rows) {
    if (_lcd) {
        delete _lcd;
        _lcd = nullptr;
    }

    Wire.begin(sdaPin, sclPin);
    Wire.setClock(100000);

    byte addr;
    bool found = false;
    uint32_t scanStart = millis();
    for (addr = 0x20; addr <= 0x27; addr++) {
        Wire.beginTransmission(addr);
        if (Wire.endTransmission() == 0) {
            if (addr == i2cAddr) {
                found = true;
                break;
            }
        }
        if (millis() - scanStart > 2000) {
            Logger::error("LCD I2C scan timeout");
            return;
        }
    }

    if (!found) {
        Logger::warn("LCD I2C device not found at 0x");
        Logger::warn(String(i2cAddr, HEX));
        return;
    }

    _lcd = new LiquidCrystal_PCF8574(i2cAddr);
    _lcd->begin(cols, rows, Wire);
    _lcd->setBacklight(255);
    _lcd->home();
    _lcd->clear();

    String msg = ssid + " " + ip;
    msg.substring(0, 48).toCharArray(_scrollBuf, 48);
    int len = strlen(_scrollBuf);
    for (int i = len; i < 48; i++) _scrollBuf[i] = ' ';

    _scrollPos = 0;
    _lastScroll = millis();
    _initialized = true;

    Logger::info("LCD I2C initialized at 0x");
    Logger::info(String(i2cAddr, HEX));
}

void LcdManager::scrollNext() {
    if (!_initialized || !_lcd) return;

    uint32_t now = millis();
    if (now - _lastScroll < 200) return;
    _lastScroll = now;

    int bufLen = strlen(_scrollBuf);
    int visible = 16;
    int maxPos = bufLen - visible;
    if (maxPos < 0) maxPos = 0;

    _lcd->home();
    for (int i = 0; i < visible && (_scrollPos + i) < bufLen; i++) {
        _lcd->write(_scrollBuf[_scrollPos + i]);
    }

    _scrollPos++;
    if (_scrollPos > maxPos) _scrollPos = 0;
}

void LcdManager::update() {
    if (!_initialized || !_lcd) return;

    scrollNext();

    long gridPower = (long)GridSensorService::currentGridPower;
    long eqPower = (long)ActuatorManager::equipmentPower;

    _lcd->setCursor(0, 1);
    char line2[17];
    snprintf(line2, sizeof(line2), "P:%ldW R:%ldW", gridPower, eqPower);
    _lcd->print(line2);
}

bool LcdManager::isEnabled() {
    return _initialized;
}
