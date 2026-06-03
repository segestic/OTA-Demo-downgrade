#include <Arduino.h>
#include "CommercialOTA.h"
#include "esp_task_wdt.h" 

#define CURRENT_VERSION "1.0.4"

// ============================================================
// MASTER COMPILE FLAG: 'W' for Wi-Fi PULL, 'B' for BLE PUSH
#define TRIGGER_MODE 'W' // // CHANGE TO B OR W
// ============================================================

// ------------------------------------------------------------
// MODE 'W': WI-FI CAPTIVE PORTAL
// ------------------------------------------------------------
#if TRIGGER_MODE == 'W' // MUST BE W

#include "CaptiveManager.h"
#define JSON_URL "https://raw.githubusercontent.com/segestic/OTA-Demo/main/manifest.json"

CaptiveManager captivePortal;
bool triggerOtaCheck = false; // Global flag requested by the Captive Dashboard
String requestedOtaVersion = ""; // NEW: Stores the user's selected OTA target

void setupMode() {
    // Start Captive Portal / Wi-Fi
    bool networkStarted = captivePortal.begin(CURRENT_VERSION);

    // THE "POINT OF NO RETURN"
    if (networkStarted) {
        OTA.markSystemStable();
    } else {
        Serial.println("[Main] Running in Setup Mode. Holding Rollback validation.");
    }
}

void loopMode() {
    captivePortal.handle();

    if (triggerOtaCheck) {
        triggerOtaCheck = false;
        
        // Kill the Async WebServer to prevent Cache Panics before we begin
        //captivePortal.stopServer();
        //Serial.println("[Main] WebServer halted. Preventing cache panics.");
        Serial.println("[Main] Starting OTA for target: " + requestedOtaVersion);
        delay(500);

        // Execute OTA synchronously
        OTA.executeOTA(JSON_URL, CURRENT_VERSION, requestedOtaVersion.c_str(), []() {
            Serial.println("[Main] Preparing system for safe Flash write...");
        });
    }
}

// ------------------------------------------------------------
// MODE 'B': BLE OTA PUSH
// ------------------------------------------------------------
#elif TRIGGER_MODE == 'B' // MUST BE B

void setupMode() {
    // Since there's no Wi-Fi setup step, if we survived bootloop detection, we are stable.
    OTA.markSystemStable();
    
    // Start the BLE GATT Server immediately so the WebApp can connect
    OTA.startBLEOTA();
}

void loopMode() {
    // BLE is handled asynchronously by FreeRTOS tasks in the background.
    // No manual polling is required here!
}

#endif
// ------------------------------------------------------------

void setup() {
    Serial.begin(115200);
    delay(1000); // Power stabilization

    // 1. Initialize OTA Protection (Heals corrupted NVS, traps boot loops)
    OTA.healNVS();
    OTA.checkBootHealth(); 
    OTA.configureWDT(15000); 

    Serial.println("[Main] Booting peripherals...");
    
    // 2. Start specific communication mode (Wi-Fi or BLE) based on macro
    setupMode();
}

void loop() {
    // "Pet the dog"
    esp_task_wdt_reset(); 

    // Process the reboot safely in the main thread! (Checklist Compliance)
    if (OTA.pendingReboot) {
        Serial.println("[SYSTEM] Safe reboot initiated...");
        delay(1000); 
        ESP.restart();
    }

    // Handle specific communication mode (Wi-Fi or BLE)
    loopMode();
    
    // Prevent FreeRTOS Idle Task starvation
    delay(10); 
}
