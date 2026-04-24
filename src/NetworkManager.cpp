#include "NetworkManager.h"
#include "Logger.h"

const Config* NetworkManager::_config = nullptr;
DNSServer NetworkManager::_dnsServer;
bool NetworkManager::_isAP = false;
uint32_t NetworkManager::_lastCheck = 0;

void NetworkManager::init(const Config& config) {
    _config = &config;
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    delay(100);

    if (config.e_wifi) {
        setupSTA();
    } else {
        setupAP();
    }
}

void NetworkManager::setupSTA() {
    Logger::log("Connecting to WiFi: " + _config->wifi_ssid);
    WiFi.mode(WIFI_STA);

    if (_config->wifi_static_ip.length() > 0) {
        IPAddress ip, gateway, subnet, dns;
        if (ip.fromString(_config->wifi_static_ip) &&
            gateway.fromString(_config->wifi_gateway) &&
            subnet.fromString(_config->wifi_subnet)) {

            dns.fromString(_config->wifi_dns.length() > 0 ? _config->wifi_dns : _config->wifi_gateway);
            WiFi.config(ip, gateway, subnet, dns);
            Logger::log("Using Static IP: " + _config->wifi_static_ip);
        }
    }

    WiFi.begin(_config->wifi_ssid.c_str(), _config->wifi_password.c_str());

    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
        delay(500);
        attempts++;
        if (attempts % 4 == 0) Serial.print(".");
    }

    if (WiFi.status() == WL_CONNECTED) {
        Logger::log("Connected! IP: " + WiFi.localIP().toString());
        _isAP = false;
        
        // Sync NTP with configured Timezone
        configTzTime(_config->timezone.c_str(), "pool.ntp.org", "time.google.com");
        Logger::log("NTP Sync started (" + _config->timezone + ")");
    } else {
        Logger::log("Connection failed. Starting Access Point...");
        setupAP();
    }
}

void NetworkManager::setupAP() {
    Logger::log("Starting Access Point: " + _config->ap_ssid);
    WiFi.mode(WIFI_AP);

    IPAddress apIP;
    apIP.fromString(_config->ap_ip);
    WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));

    WiFi.softAP(_config->ap_ssid.c_str(), _config->ap_password.c_str(), _config->ap_channel, _config->ap_hidden_ssid);

    Logger::log("AP IP: " + WiFi.softAPIP().toString());
    _isAP = true;
    startCaptivePortal();
}

void NetworkManager::startCaptivePortal() {
    _dnsServer.start(53, "*", WiFi.softAPIP());
    Logger::log("Captive Portal DNS started");
}

void NetworkManager::loop() {
    if (_isAP) {
        _dnsServer.processNextRequest();
    } else {
        if (millis() - _lastCheck > 30000) {
            _lastCheck = millis();
            if (WiFi.status() != WL_CONNECTED) {
                Logger::log("WiFi connection lost. Reconnecting...");
                WiFi.reconnect();
            }
        }
    }
}

bool NetworkManager::isConnected() {
    return _isAP || (WiFi.status() == WL_CONNECTED);
}

String NetworkManager::getIP() {
    return _isAP ? WiFi.softAPIP().toString() : WiFi.localIP().toString();
}
