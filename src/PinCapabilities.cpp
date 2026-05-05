#include "PinCapabilities.h"
#include <stddef.h>

namespace {

static bool isInList(int pin, const int* arr, size_t n) {
    for (size_t i = 0; i < n; ++i) {
        if (arr[i] == pin) return true;
    }
    return false;
}

static bool isOutputRole(PinRole role) {
    return role == PinRole::SSR || role == PinRole::RELAY || role == PinRole::FAN_PWM || role == PinRole::INTERNAL_LED;
}

static bool isInputRole(PinRole role) {
    return role == PinRole::ZX_INPUT || role == PinRole::DS18B20 || role == PinRole::JSY_RX;
}

static bool isTxRole(PinRole role) {
    return role == PinRole::JSY_TX;
}

} // namespace

const char* pinRoleName(PinRole role) {
    switch (role) {
        case PinRole::SSR: return "ssr_pin";
        case PinRole::RELAY: return "relay_pin";
        case PinRole::FAN_PWM: return "fan_pin";
        case PinRole::ZX_INPUT: return "zx_pin";
        case PinRole::DS18B20: return "ds18b20_pin";
        case PinRole::INTERNAL_LED: return "internal_led_pin";
        case PinRole::JSY_TX: return "jsy_tx";
        case PinRole::JSY_RX: return "jsy_rx";
    }
    return "pin";
}

String pinValidationReason(int pin, PinRole role) {
    if (pin < 0) return "negative pin is invalid";

#if defined(CONFIG_IDF_TARGET_ESP32S3)
    if (!((pin >= 0 && pin <= 21) || (pin >= 26 && pin <= 48))) {
        return "not a valid ESP32-S3 GPIO (expected 0-21 or 26-48)";
    }

    static const int kGlobalBlocked[] = {0, 19, 20, 26, 27, 28, 29, 30, 31, 32, 45, 46};
    if (isInList(pin, kGlobalBlocked, sizeof(kGlobalBlocked) / sizeof(kGlobalBlocked[0]))) {
        return "blocked on ESP32-S3 (boot/USB/flash-psram/special pin)";
    }

    if (role == PinRole::ZX_INPUT && pin == 48) {
        return "avoid GPIO48 for zero-crossing (often tied to onboard LED/strapping behavior)";
    }

    if (isTxRole(role) && pin == 47) {
        return "avoid GPIO47 for UART TX (USB/JTAG interference risk on some S3 boards)";
    }

#elif defined(CONFIG_IDF_TARGET_ESP32)
    if (pin > 39) {
        return "not a valid ESP32 GPIO (expected 0-39)";
    }

    static const int kGlobalBlocked[] = {6, 7, 8, 9, 10, 11, 20, 24, 28, 29, 30, 31};
    if (isInList(pin, kGlobalBlocked, sizeof(kGlobalBlocked) / sizeof(kGlobalBlocked[0]))) {
        return "blocked on ESP32 (flash or non-existent pin)";
    }

    if (isOutputRole(role) || isTxRole(role)) {
        if (pin >= 34 && pin <= 39) {
            return "input-only pin cannot be used as output";
        }
        static const int kOutputRisk[] = {0, 2, 4, 5, 12, 15};
        if (isInList(pin, kOutputRisk, sizeof(kOutputRisk) / sizeof(kOutputRisk[0]))) {
            return "strapping/risky boot pin for output role";
        }
    }

    if (isInputRole(role)) {
        static const int kInputRisk[] = {0, 2, 4, 5, 12, 15};
        if (isInList(pin, kInputRisk, sizeof(kInputRisk) / sizeof(kInputRisk[0]))) {
            return "strapping/risky boot pin for input role";
        }
    }
#else
    if (pin > 48) return "unsupported target pin range";
#endif

    return "";
}

bool isPinValidForRole(int pin, PinRole role) {
    return pinValidationReason(pin, role).length() == 0;
}
