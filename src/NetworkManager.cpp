#include "NetworkManager.h"
#include "Logger.h"

const Config* NetworkManager::_config = nullptr;
DNSServer NetworkManager::_dnsServer;
bool NetworkManager::_isAP = false;
uint32_t NetworkManager::_lastCheck = 0;
bool NetworkManager::_cachedConnected = false;
int NetworkManager::_cachedRSSI = -100;

void NetworkManager::init(const Config& config) {
    _config = &config;
    _cachedConnected = false;
    _cachedRSSI = -100;
    
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
    Logger::info("Connecting to WiFi SSID: [" + _config->wifi_ssid + "]");
    WiFi.mode(WIFI_STA);

    if (_config->wifi_static_ip.length() > 0) {
        IPAddress ip, gateway, subnet, dns;
        if (ip.fromString(_config->wifi_static_ip) &&
            gateway.fromString(_config->wifi_gateway) &&
            subnet.fromString(_config->wifi_subnet)) {

            dns.fromString(_config->wifi_dns.length() > 0 ? _config->wifi_dns : _config->wifi_gateway);
            WiFi.config(ip, gateway, subnet, dns);
            Logger::info("Using Static IP: " + _config->wifi_static_ip);
        }
    }

    WiFi.begin(_config->wifi_ssid.c_str(), _config->wifi_password.c_str());

    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 30) { // Increased to 15s
        delay(500);
        attempts++;
        if (attempts % 2 == 0) Serial.print(".");
    }
    Serial.println();

    if (WiFi.status() == WL_CONNECTED) {
        WiFi.setSleep(false); // Disable power save for stability
        WiFi.setTxPower(WIFI_POWER_19_5dBm);
        Logger::info("Connected! IP: " + WiFi.localIP().toString());
        _isAP = false;
        
        // Sync NTP with configured Timezone
        configTzTime(_config->timezone.c_str(), "pool.ntp.org", "time.google.com");
        Logger::info("NTP Sync started (" + _config->timezone + ")");
    } else {
        Logger::warn("Connection failed (Status: " + String(WiFi.status()) + "). Starting Access Point...");
        setupAP();
    }
}

void NetworkManager::setupAP() {
    Logger::info("Starting Access Point: " + _config->ap_ssid);
    WiFi.mode(WIFI_AP);

    IPAddress apIP;
    apIP.fromString(_config->ap_ip);
    WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));

    WiFi.softAP(_config->ap_ssid.c_str(), _config->ap_password.c_str(), _config->ap_channel, _config->ap_hidden_ssid);
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
    } else {
        uint32_t now = millis();
        if (now - _lastCheck > 1000) { // Update cache every 1s
            _lastCheck = now;
            _cachedConnected = (WiFi.status() == WL_CONNECTED);
            if (_cachedConnected) {
                _cachedRSSI = WiFi.RSSI();
            } else {
                Logger::warn("WiFi connection lost. Reconnecting...");
                WiFi.reconnect();
                _cachedRSSI = -100;
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
    return _isAP ? WiFi.softAPIP().toString() : WiFi.localIP().toString();
}
