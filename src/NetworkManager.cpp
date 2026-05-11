#include "NetworkManager.h"
#include "Logger.h"
#include <esp_task_wdt.h>

const Config* NetworkManager::_config = nullptr;
bool NetworkManager::_isAP = false;
uint32_t NetworkManager::_lastCheck = 0;
bool NetworkManager::_cachedConnected = false;
int NetworkManager::_cachedRSSI = -100;

// Back-off state for reconnect attempts
static uint32_t _lastReconnectMs = 0;
static uint8_t _reconnectFailures = 0;
static const uint32_t RECONNECT_INTERVAL_MS = 10000; // retry every 10 s
static const uint8_t  RECONNECT_FAILS_BEFORE_AP = 12; // ~2 min sustained outage -> AP fallback

void NetworkManager::init(const Config& config) {
    _config = &config;
    _cachedConnected = false;
    _cachedRSSI = -100;
    _lastCheck = millis();
    _lastReconnectMs = millis();
    _reconnectFailures = 0;

    // Force-disable persistent settings so the SDK doesn't auto-start an AP
    // that was active during a previous session.
    WiFi.persistent(false);
    WiFi.disconnect(true);
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_OFF);
    delay(100);

    if (config.e_wifi) {
        setupSTA();
    } else {
        Logger::warn("WiFi is disabled by user configuration. Starting AP.");
        setupAP();
    }
}

void NetworkManager::setupSTA() {
    if (_config == nullptr) {
        Serial.println("[ERROR] NetworkManager::setupSTA called before init");
        return;
    }
    Logger::info("Connecting to WiFi SSID: [" + _config->wifi_ssid + "]");

    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_STA);

    if (_config->wifi_static_ip.length() > 0) {
        IPAddress ip, gateway, subnet, dns;
        bool ok_ip      = ip.fromString(_config->wifi_static_ip);
        bool ok_gw      = gateway.fromString(_config->wifi_gateway);
        bool ok_sn      = subnet.fromString(_config->wifi_subnet);

        if (ok_ip && ok_gw && ok_sn) {
            const String& dnsStr = _config->wifi_dns.length() > 0 ? _config->wifi_dns : _config->wifi_gateway;
            if (!dns.fromString(dnsStr)) {
                Logger::warn("Invalid DNS '" + dnsStr + "', falling back to gateway");
                dns = gateway;
            }
            WiFi.config(ip, gateway, subnet, dns);
            Logger::info("Using Static IP: " + _config->wifi_static_ip);
        } else {
            Logger::warn("Static IP config invalid (ip/gw/subnet parse failed), using DHCP");
        }
    }

    WiFi.begin(_config->wifi_ssid.c_str(), _config->wifi_password.c_str());

    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 30) { // up to 15s
        delay(500);
        attempts++;
        if (attempts % 2 == 0) Serial.print(".");
        if (esp_task_wdt_status(NULL) == ESP_OK) {
            esp_task_wdt_reset();
        }
    }
    Serial.println();

    if (WiFi.status() == WL_CONNECTED) {
        WiFi.setSleep(false);
        WiFi.setTxPower(WIFI_POWER_19_5dBm);
        Logger::info("Connected! IP: " + WiFi.localIP().toString());
        _isAP = false;
        _cachedConnected = true;
        _cachedRSSI = WiFi.RSSI();
        _reconnectFailures = 0;

        configTzTime(_config->timezone.c_str(), "pool.ntp.org", "time.google.com");
        Logger::info("NTP Sync started (" + _config->timezone + ")");
    } else {
        char buf[80];
        snprintf(buf, sizeof(buf), "Connection failed (Status: %d). Starting fallback AP.", (int)WiFi.status());
        Logger::warn(String(buf));
        setupAP();
    }
}

void NetworkManager::setupAP() {
    if (_isAP) return;
    if (_config == nullptr) {
        Serial.println("[ERROR] NetworkManager::setupAP called before init");
        return;
    }
    if (_config->ap_ssid.length() == 0) {
        Logger::error("AP SSID is empty, cannot start Access Point");
        return;
    }

    Logger::info("Starting Access Point: " + _config->ap_ssid);

    // Use WIFI_AP_STA when e_wifi is enabled so we can keep trying STA in background.
    // Use WIFI_AP when e_wifi is disabled (pure AP, no STA reconnect needed).
    WiFi.mode(_config->e_wifi ? WIFI_AP_STA : WIFI_AP);
    _isAP = true;   /* set early — avoids a "halfway" state between mode change and softAP() */

    IPAddress apIP;
    if (!apIP.fromString(_config->ap_ip)) {
        Logger::warn("Invalid ap_ip '" + _config->ap_ip + "', falling back to 192.168.4.1");
        apIP = IPAddress(192, 168, 4, 1);
    }
    if (!WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0))) {
        Logger::warn("softAPConfig failed");
    }

    int ch = _config->ap_channel;
    if (ch < 1 || ch > 13) {
        Logger::warn("Invalid ap_channel " + String(ch) + ", forcing 1");
        ch = 1;
    }

    bool ok = WiFi.softAP(_config->ap_ssid.c_str(), _config->ap_password.c_str(), ch, _config->ap_hidden_ssid);
    if (!ok) {
        Logger::error("WiFi.softAP() failed to start AP");
        return;
    }
    WiFi.setSleep(false);
    Logger::info("AP IP: " + WiFi.softAPIP().toString());
    _isAP = true;
}

void NetworkManager::loop() {
    // When AP is active and e_wifi is enabled, check if STA recovered.
    if (_isAP && _config && _config->e_wifi) {
        if (WiFi.status() == WL_CONNECTED) {
            Logger::info("WiFi recovered, stopping AP fallback");
            _isAP = false;
            WiFi.softAPdisconnect(true);
            WiFi.mode(WIFI_STA);
            _reconnectFailures = 0;
            return;
        }
    }

    if (_config == nullptr) return;

    // If we are in AP mode and STA is NOT enabled, nothing else to do.
    if (_isAP && !_config->e_wifi) return;

    uint32_t now = millis();
    if (now - _lastCheck > 1000) {
        _lastCheck = now;

        // Defensive: if NOT in AP mode, ensure the radio hasn't flipped to AP_STA
        if (!_isAP && WiFi.getMode() != WIFI_STA) {
            WiFi.softAPdisconnect(true);
            WiFi.mode(WIFI_STA);
        }

        _cachedConnected = (WiFi.status() == WL_CONNECTED);
        if (_cachedConnected) {
            _cachedRSSI = WiFi.RSSI();
            _reconnectFailures = 0;
        } else {
            _cachedRSSI = -100;
            if (now - _lastReconnectMs >= RECONNECT_INTERVAL_MS) {
                _lastReconnectMs = now;
                _reconnectFailures++;
                Logger::warn("WiFi disconnected (attempt " + String(_reconnectFailures) + "), reconnecting...");
                WiFi.reconnect();

                if (_reconnectFailures >= RECONNECT_FAILS_BEFORE_AP) {
                    Logger::error("WiFi reconnect failed " + String(_reconnectFailures) + " times. Starting fallback AP.");
                    _reconnectFailures = 0;
                    setupAP();
                }

                /* Always refresh RSSI after WiFi.reconnect() so callers see current signal */
                _cachedRSSI = WiFi.RSSI();
            }
        }
    }
}

bool NetworkManager::isConnected() {
    return _isAP || _cachedConnected;
}

int NetworkManager::getRSSI() {
    return _cachedRSSI;
}

String NetworkManager::getIP() {
    if (_isAP) return WiFi.softAPIP().toString();
    if (!_cachedConnected) return String("0.0.0.0");
    return WiFi.localIP().toString();
}
