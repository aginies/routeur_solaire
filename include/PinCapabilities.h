#ifndef PINCAPABILITIES_H
#define PINCAPABILITIES_H

#include "Config.h"

enum class PinRole {
    SSR,
    RELAY,
    FAN_PWM,
    ZX_INPUT,
    DS18B20,
    INTERNAL_LED,
    JSY1_TX,
    JSY1_RX,
    JSY2_TX,
    JSY2_RX,
    LCD_SDA,
    LCD_SCL,
};

bool isPinValidForRole(int pin, PinRole role);
String pinValidationReason(int pin, PinRole role);
const char* pinRoleName(PinRole role);
bool isI2cAddressValid(byte addr);

#endif
