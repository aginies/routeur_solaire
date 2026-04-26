#ifndef NETWORKMANAGER_H
#define NETWORKMANAGER_H

#include <WiFi.h>
#include <DNSServer.h>
#include "Config.h"

class NetworkManager {
public:
    static void init(const Config& config);
    static void loop();
    static bool isConnected();
    static String getIP();
    static int getRSSI();

private:
    static void setupSTA();
    static void setupAP();
    static void startCaptivePortal();

    static const Config* _config;
    static DNSServer _dnsServer;
    static bool _isAP;
    static uint32_t _lastCheck;
    static bool _cachedConnected;
    static int _cachedRSSI;
};

#endif
