#include "CommercialOTA.h"
#include "ESP32OTAPull.h"
#include <WiFi.h>
#include "nvs_flash.h"
#include "esp_ota_ops.h"
#include "esp_task_wdt.h"

// --- ADDED FOR BLE OTA PUSH ---
#include <Update.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

// ============================================================
// HIDDEN RTC VARIABLES (Survive soft reboots, hidden from .ino)
// ============================================================
RTC_DATA_ATTR int ota_bootCrashCounter = 0;
RTC_DATA_ATTR bool ota_intentionalReboot = false;
RTC_DATA_ATTR unsigned long ota_stabilityGuard = 0;
#define STABILITY_MAGIC_NUM 0x12345678

CommercialOTA OTA; // Instantiate the global object

CommercialOTA::CommercialOTA() : _isStable(false) {}

void CommercialOTA::healNVS() {
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        Serial.println("[OTA_MOD] NVS Corrupted! Reformatting...");
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
}

void CommercialOTA::configureWDT(uint32_t timeout_ms) {
    // We go back to exactly how your working code initializes the WDT
    #if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
        esp_task_wdt_config_t twdt_config = {
            .timeout_ms = timeout_ms,
            .idle_core_mask = (1 << portNUM_PROCESSORS) - 1,
            .trigger_panic = true
        };
        esp_task_wdt_init(&twdt_config); 
    #else
        esp_task_wdt_init(timeout_ms / 1000, true);
    #endif
    
    // Subscribe the main setup/loop task to the WDT
    esp_task_wdt_add(NULL);
}

void CommercialOTA::executeOTA(const char* manifestUrl, const char* currentVersion, const char* targetConfig, std::function<void()> preOtaHook) {
    Serial.printf("[OTA_MOD] Starting Safe OTA Pull sequence for target '%s'...\n", targetConfig ? targetConfig : "(none)");

    if (targetConfig == nullptr || strlen(targetConfig) == 0) {
        Serial.println("[OTA_MOD] No targetConfig specified! Aborting.");
        this->otaState = 2;
        return;
    }

    // Fire hook (used to kill the WebServer to prevent Cache panics)
    if (preOtaHook) {
        preOtaHook();
    }

    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[OTA_MOD] No active Wi-Fi connection! Aborting.");
        this->otaState = 2; // Tell UI it failed
        return;
    }

    // INDUSTRIAL RULE 1: Pause Watchdog
    // (This will now safely succeed because configureWDT() initialized it properly)
    esp_task_wdt_delete(NULL); 

    // Cache-Bust the URL
    String live_url = String(manifestUrl) + "?v=" + String(millis());

    // 1. Tell the UI that we are officially DOWNLOADING
    this->otaState = 1; 
    this->otaProgress = 0;

    ESP32OTAPull puller;
    int ret = puller
        .SetCallback([](int offset, int totallength) {
            int percent = 100 * offset / totallength;
            OTA.otaProgress = percent;

            static int lastPercent = 0;
            if (percent >= lastPercent + 10) {
                Serial.printf("[OTA_MOD] Downloading... %02d%%\n", percent);
                lastPercent = percent;
            }
        })
        .OverrideBoard("ESP32_DEV")
        .SetConfig(targetConfig)
        .AllowDowngrades(true)
        .CheckForOTAUpdate(live_url.c_str(), currentVersion, ESP32OTAPull::UPDATE_AND_BOOT);
        
    // Handle Failure 
    if (ret != ESP32OTAPull::UPDATE_OK) {
        Serial.printf("[OTA_MOD] Update failed (Code: %d). Rebooting cleanly...\n", ret);
        this->otaState = 2; // 4. Tell UI it failed
        setIntentionalReboot(); 
        delay(1000);
        ESP.restart();
    }

    esp_task_wdt_add(NULL); 
}

void CommercialOTA::checkBootHealth(uint8_t maxCrashes) {
    esp_reset_reason_t reason = esp_reset_reason();
    bool isColdBoot = (reason == ESP_RST_POWERON || reason == ESP_RST_BROWNOUT);

    if (ota_intentionalReboot) {
        ota_intentionalReboot = false; 
        ota_bootCrashCounter = 0;      
    } else if (ota_stabilityGuard == STABILITY_MAGIC_NUM) {
        ota_bootCrashCounter = 0; 
    } else if (isColdBoot) {
        ota_bootCrashCounter = 0;
    } else {
        ota_bootCrashCounter++;
    }
    
    ota_stabilityGuard = 0; // Reset so this new boot must prove itself

    if (ota_bootCrashCounter >= maxCrashes) {
        _trapBootLoop(ota_bootCrashCounter);
    }
}

void CommercialOTA::_trapBootLoop(uint8_t crashCount) {
    Serial.printf("\n[FATAL] Boot Loop Detected! Crashes: %d.\n", crashCount);
    Serial.println("[SYSTEM] Hardware Rollback Triggered. Halting execution...");
    while(true) { 
        // Freezes here. The Watchdog will bite, and the bootloader 
        // will roll back the active partition to the last working version.
        delay(100); 
    }
}

void CommercialOTA::setIntentionalReboot() {
    ota_intentionalReboot = true;
}

void CommercialOTA::markSystemStable() {
    if (!_isStable) {
        esp_ota_mark_app_valid_cancel_rollback();
        ota_stabilityGuard = STABILITY_MAGIC_NUM;
        _isStable = true;
        Serial.println("[OTA_MOD] Firmware validated. Rollback cleared.");
    }
}

// ============================================================
// ADDED: BLE PUSH OTA EXECUTION 
// ============================================================
#define OTA_SERVICE_UUID "8a7f1168-48af-4efb-83b5-e679f9320001"
#define OTA_CTRL_CH_UUID "8a7f1168-48af-4efb-83b5-e679f9320002"
#define OTA_DATA_CH_UUID "8a7f1168-48af-4efb-83b5-e679f9320003"

static bool bleUpdating = false;
static size_t bleTotalBytes = 0;

class OtaCtrlCallbacks : public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic* pChar) override {
        // Core v3: getValue() returns an Arduino String directly
        String data = pChar->getValue().c_str();
        
        if (data.startsWith("START:")) {
            int fileSize = data.substring(6).toInt();
            Serial.printf("[BLE_OTA] Starting Push Update. Size: %d bytes\n", fileSize);
            
            esp_task_wdt_delete(NULL); // Pause watchdog during flash writing
            
            if (Update.begin(fileSize, U_FLASH)) {
                bleUpdating = true;
                bleTotalBytes = 0;
                Serial.println("[BLE_OTA] Ready for data streams.");
            }
        } 
        else if (data == "END") {
            if (Update.end(true)) {
                Serial.println("[BLE_OTA] Update Success! Flagging for reboot...");
                OTA.setIntentionalReboot();
                // FIX: Set flag instead of blocking inside callback! 
                // The main loop will handle the delay and restart.
                OTA.pendingReboot = true; 
            } else {
                Serial.println("[BLE_OTA] Update Failed.");
                Update.printError(Serial);
                bleUpdating = false;
                esp_task_wdt_add(NULL); // Restore watchdog on failure
            }
        }
    }
};

class OtaDataCallbacks : public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic* pChar) override {
        if (!bleUpdating) return;
        
        // FIX: Core v3 uses raw pointers for binary firmware chunks to prevent null-byte corruption
        uint8_t* rxData = pChar->getData();
        size_t rxLen = pChar->getLength();
        
        if (rxLen > 0 && rxData != nullptr) {
            size_t written = Update.write(rxData, rxLen);
            bleTotalBytes += written;
            
            if (bleTotalBytes % 51200 < rxLen) { // Log every ~50KB to prevent Serial buffer overflow
                Serial.printf("[BLE_OTA] Written: %u bytes\n", bleTotalBytes);
            }
        }
    }
};

void CommercialOTA::startBLEOTA() {
    Serial.println("[BLE_OTA] Initializing BLE Server for direct Push...");
    
    BLEDevice::init("ESP32_Industrial");
    BLEDevice::setMTU(517); // Crucial for transfer speed

    BLEServer *pServer = BLEDevice::createServer();
    BLEService *pService = pServer->createService(OTA_SERVICE_UUID);

    BLECharacteristic *pCtrlChar = pService->createCharacteristic(
        OTA_CTRL_CH_UUID, BLECharacteristic::PROPERTY_WRITE
    );
    pCtrlChar->setCallbacks(new OtaCtrlCallbacks());

    BLECharacteristic *pDataChar = pService->createCharacteristic(
        OTA_DATA_CH_UUID, BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR
    );
    pDataChar->setCallbacks(new OtaDataCallbacks());

    pService->start();
    BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
    pAdvertising->addServiceUUID(OTA_SERVICE_UUID);
    pAdvertising->setScanResponse(true);
    BLEDevice::startAdvertising();
    
    Serial.println("[BLE_OTA] BLE Active. Connect via Web Bluetooth App to push firmware.");
}
