#include "NetworkManager.h"
#include "Logger.h"
#include <esp_task_wdt.h>

const Config* NetworkManager::_config = nullptr;
DNSServer NetworkManager::_dnsServer;
bool NetworkManager::_isAP = false;
uint32_t NetworkManager::_lastCheck = 0;
bool NetworkManager::_cachedConnected = false;
int NetworkManager::_cachedRSSI = -100;

// Bug #2: back-off state for reconnect attempts
static uint32_t _lastReconnectMs = 0;
static uint8_t _reconnectFailures = 0;
static const uint32_t RECONNECT_INTERVAL_MS = 10000; // retry every 10 s, not every 1 s
static const uint8_t  RECONNECT_FAILS_BEFORE_AP = 12; // ~2 min sustained outage -> AP fallback

void NetworkManager::init(const Config& config) {
    _config = &config;
    _cachedConnected = false;
    _cachedRSSI = -100;
    _lastCheck = millis();          // Bug #5: prime so first loop() doesn't immediately reconnect
    _lastReconnectMs = millis();
    _reconnectFailures = 0;

    // We avoid aggressive disconnect/off here to see if it helps WROOM stability
    // WiFi.disconnect(true);
    // WiFi.mode(WIFI_OFF);
    // delay(100);

    if (config.e_wifi) {
        setupSTA();
    } else {
        setupAP();
    }
}

void NetworkManager::setupSTA() {
    if (_config == nullptr) { // Bug #7
        Serial.println("[ERROR] NetworkManager::setupSTA called before init");
        return;
    }
    Logger::info("Connecting to WiFi SSID: [" + _config->wifi_ssid + "]");
    WiFi.mode(WIFI_STA);

    if (_config->wifi_static_ip.length() > 0) {
        IPAddress ip, gateway, subnet, dns;
        bool ok_ip      = ip.fromString(_config->wifi_static_ip);
        bool ok_gw      = gateway.fromString(_config->wifi_gateway);
        bool ok_sn      = subnet.fromString(_config->wifi_subnet);

        if (ok_ip && ok_gw && ok_sn) {
            // Bug #4: validate DNS parse before passing to WiFi.config().
            const String& dnsStr = _config->wifi_dns.length() > 0 ? _config->wifi_dns : _config->wifi_gateway;
            if (!dns.fromString(dnsStr)) {
                Logger::warn("Invalid DNS '" + dnsStr + "', falling back to gateway");
                dns = gateway;
            }
            WiFi.config(ip, gateway, subnet, dns);
            Logger::info("Using Static IP: " + _config->wifi_static_ip);
        } else {
            // Bug #6: log silent fallback to DHCP
            Logger::warn("Static IP config invalid (ip/gw/subnet parse failed), using DHCP");
        }
    }

    WiFi.begin(_config->wifi_ssid.c_str(), _config->wifi_password.c_str());

    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 30) { // up to 15s
        delay(500);
        attempts++;
        if (attempts % 2 == 0) Serial.print(".");
        // Bug #1: defensive WDT pet if calling task is subscribed
        if (esp_task_wdt_status(NULL) == ESP_OK) {
            esp_task_wdt_reset();
        }
    }
    Serial.println();

    if (WiFi.status() == WL_CONNECTED) {
        WiFi.setSleep(false); // Disable power save for stability
        WiFi.setTxPower(WIFI_POWER_19_5dBm);
        Logger::info("Connected! IP: " + WiFi.localIP().toString());
        _isAP = false;
        _cachedConnected = true;        // prime the cache
        _cachedRSSI = WiFi.RSSI();
        _reconnectFailures = 0;

        // Sync NTP with configured Timezone
        configTzTime(_config->timezone.c_str(), "pool.ntp.org", "time.google.com");
        Logger::info("NTP Sync started (" + _config->timezone + ")");
    } else {
        // Bug #11: snprintf instead of String + String(int)
        char buf[80];
        snprintf(buf, sizeof(buf), "Connection failed (Status: %d). Starting Access Point...", (int)WiFi.status());
        Logger::warn(String(buf));
        setupAP();
    }
}

void NetworkManager::setupAP() {
    if (_config == nullptr) { // Bug #7
        Serial.println("[ERROR] NetworkManager::setupAP called before init");
        return;
    }
    // Bug #13: don't try to start AP without an SSID
    if (_config->ap_ssid.length() == 0) {
        Logger::error("AP SSID is empty, cannot start Access Point");
        return;
    }

    Logger::info("Starting Access Point: " + _config->ap_ssid);
    WiFi.mode(WIFI_AP);

    // Bug #3: validate ap_ip; fall back to 192.168.4.1 if malformed.
    IPAddress apIP;
    if (!apIP.fromString(_config->ap_ip)) {
        Logger::warn("Invalid ap_ip '" + _config->ap_ip + "', falling back to 192.168.4.1");
        apIP = IPAddress(192, 168, 4, 1);
    }
    if (!WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0))) {
        Logger::warn("softAPConfig failed");
    }

    // Bug #9: validate ap_channel (1..13). 14 is JP-only and many radios reject it.
    int ch = _config->ap_channel;
    if (ch < 1 || ch > 13) {
        Logger::warn("Invalid ap_channel " + String(ch) + ", forcing 1");
        ch = 1;
    }

    // Bug #8: capture softAP() return
    bool ok = WiFi.softAP(_config->ap_ssid.c_str(), _config->ap_password.c_str(), ch, _config->ap_hidden_ssid);
    if (!ok) {
        Logger::error("WiFi.softAP() failed to start AP");
        return;
    }
    WiFi.setSleep(false);
    Logger::info("AP IP: " + WiFi.softAPIP().toString());
    _isAP = true;
    startCaptivePortal();
}

void NetworkManager::startCaptivePortal() {
    _dnsServer.start(53, "*", WiFi.softAPIP());
    Logger::info("Captive Portal DNS started");
}

void NetworkManager::loop() {
    if (_isAP) {
        _dnsServer.processNextRequest();
        return;
    }

    if (_config == nullptr) return; // Bug #7

    uint32_t now = millis();
    if (now - _lastCheck > 1000) { // Update cache every 1s
        _lastCheck = now;
        _cachedConnected = (WiFi.status() == WL_CONNECTED);
        if (_cachedConnected) {
            _cachedRSSI = WiFi.RSSI();
            _reconnectFailures = 0; // Bug #2: reset back-off counter when healthy
        } else {
            _cachedRSSI = -100;
            // Bug #2: throttle reconnect attempts and fall back to AP after sustained failure.
            if (now - _lastReconnectMs >= RECONNECT_INTERVAL_MS) {
                _lastReconnectMs = now;
                _reconnectFailures++;
                Logger::warn("WiFi disconnected (attempt " + String(_reconnectFailures) + "), reconnecting...");
                WiFi.reconnect();

                if (_reconnectFailures >= RECONNECT_FAILS_BEFORE_AP) {
                    Logger::error("WiFi reconnect failed " + String(_reconnectFailures) + " times, falling back to AP mode");
                    _reconnectFailures = 0;
                    setupAP(); // sets _isAP = true
                }
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
    if (!_cachedConnected) return String("0.0.0.0"); // Bug #10: explicit not-connected sentinel
    return WiFi.localIP().toString();
}
