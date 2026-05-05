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
    JSY_TX,
    JSY_RX,
};

bool isPinValidForRole(int pin, PinRole role);
String pinValidationReason(int pin, PinRole role);
const char* pinRoleName(PinRole role);

#endif
