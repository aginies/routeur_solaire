#ifndef LCDMANAGER_H
#define LCDMANAGER_H

#include <Arduino.h>

class LcdManager {
public:
    static void init(const String& ssid, const String& ip, byte i2cAddr, int sdaPin, int sclPin);
    static void update();
    static bool isEnabled();

private:
    static void scrollNext();

    static bool _initialized;
    static char _scrollBuf[48];
    static int _scrollPos;
    static uint32_t _lastScroll;
};

#endif
