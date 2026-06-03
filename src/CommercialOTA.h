#ifndef COMMERCIAL_OTA_H
#define COMMERCIAL_OTA_H

#include <Arduino.h>
#include <functional>

class CommercialOTA {
public:
    CommercialOTA();

    void healNVS();
    void configureWDT(uint32_t timeout_ms = 15000);
    
    void checkBootHealth(uint8_t maxCrashes = 5);
    void setIntentionalReboot();
    void markSystemStable();

    // Trigger Wi-Fi OTA (Pull from GitHub)
    void executeOTA(const char* manifestUrl, const char* currentVersion, const char* targetConfig, std::function<void()> preOtaHook = nullptr);

    // Trigger BLE OTA (Push from Web Bluetooth)
    void startBLEOTA();

    // Safe Reboot Flag (Industrial Checklist Fix: Never block inside a callback)
    volatile bool pendingReboot = false;
    
    // Live UI Trackers
    volatile int otaProgress = 0;
    volatile int otaState = 0; // 0=Idle, 1=Downloading, 2=Failed

private:
    bool _isStable;
    void _trapBootLoop(uint8_t crashCount);
};

extern CommercialOTA OTA;

#endif