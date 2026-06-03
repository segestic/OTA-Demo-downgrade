#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <DNSServer.h>
#include <ESPAsyncWebServer.h>
#include <Preferences.h>
#include "CommercialOTA.h" 

extern bool triggerOtaCheck; 
extern String requestedOtaVersion;

class CaptiveManager {
private:
    AsyncWebServer server;
    DNSServer dnsServer;
    Preferences preferences;
    bool portalActive;
    const byte DNS_PORT = 53;

    bool pendingSave = false;
    unsigned long saveTriggerTime = 0;
    String pendingSSID = "";
    String pendingPASS = "";

    bool pendingReset = false;
    unsigned long resetTriggerTime = 0;

    void startPortal() {
        portalActive = true;
        
        WiFi.mode(WIFI_AP);
        WiFi.softAP("ESP32_Commercial_Setup");
        Serial.println("[CAPTIVE] Started Access Point: ESP32_Commercial_Setup");

        dnsServer.start(DNS_PORT, "*", WiFi.softAPIP());

        server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
            String html = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8"><meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>Device Setup</title>
  <script src="https://cdn.tailwindcss.com"></script>
  <script src="https://unpkg.com/lucide@latest"></script>
  <link href="https://fonts.googleapis.com/css2?family=Inter:wght@400;600;800&display=swap" rel="stylesheet">
  <style>
    body { font-family: 'Inter', sans-serif; background-color: #0a0a0f; }
    .bg-mesh { position: fixed; inset: 0; z-index: -1; background: radial-gradient(circle 500px at 20% 20%, rgba(99,102,241,0.15), transparent), radial-gradient(circle 400px at 80% 70%, rgba(139,92,246,0.1), transparent); }
  </style>
</head>
<body class="min-h-screen text-slate-200 flex items-center justify-center p-4 relative overflow-hidden">
  <div class="bg-mesh"></div>
  <div class="absolute inset-0 opacity-[0.02] pointer-events-none" style="background-image: radial-gradient(#fff 1px, transparent 0); background-size: 24px 24px;"></div>
  
  <div class="w-full max-w-sm z-10">
    <div class="bg-white/[0.03] border border-white/5 rounded-3xl p-8 backdrop-blur-xl shadow-2xl text-center">
      <div class="inline-flex items-center justify-center w-12 h-12 rounded-full bg-indigo-500/10 mb-4 text-indigo-400">
        <i data-lucide="wifi"></i>
      </div>
      <h2 class="text-2xl font-bold text-white mb-6">Device Setup</h2>
      <form action='/save' method='POST' class="space-y-4">
        <input type='text' name='ssid' placeholder='WiFi Name (SSID)' required class="w-full bg-black/30 border border-white/10 rounded-xl px-4 py-3 text-white placeholder-slate-500 focus:outline-none focus:border-indigo-500 focus:ring-1 focus:ring-indigo-500 transition-all">
        <input type='password' name='pass' placeholder='Password' required class="w-full bg-black/30 border border-white/10 rounded-xl px-4 py-3 text-white placeholder-slate-500 focus:outline-none focus:border-indigo-500 focus:ring-1 focus:ring-indigo-500 transition-all">
        <button type='submit' class="w-full flex items-center justify-center gap-2 py-3 rounded-xl bg-gradient-to-r from-indigo-500 to-purple-600 hover:from-indigo-600 hover:to-purple-700 text-white font-bold transition-all transform hover:-translate-y-0.5 mt-2">
          Connect Device <i data-lucide="arrow-right" class="w-4 h-4"></i>
        </button>
      </form>
    </div>
  </div>
  <script>lucide.createIcons();</script>
</body>
</html>
)rawliteral";
            request->send(200, "text/html", html);
        });

        server.on("/save", HTTP_POST, [this](AsyncWebServerRequest *request){
            this->pendingSSID = request->arg("ssid");
            this->pendingPASS = request->arg("pass");
            request->send(200, "text/html", "<body style='background:#0a0a0f;color:white;text-align:center;padding:50px;font-family:sans-serif;'><h2>Credentials Received!</h2><p>Device is rebooting to connect...</p></body>");
            this->pendingSave = true;
            this->saveTriggerTime = millis();
        });

        server.onNotFound([](AsyncWebServerRequest *request){
            request->redirect(String("http://") + WiFi.softAPIP().toString() + "/");
        });

        server.begin();
    }

public:
    CaptiveManager() : server(80), portalActive(false) {}

    // Expose this so the main loop can kill the server before flashing!
    void stopServer() {
        server.end();
    }

    bool begin(String currentVersion) {
        preferences.begin("wifi_creds", true); 
        String savedSSID = preferences.getString("ssid", "");
        String savedPASS = preferences.getString("pass", "");
        preferences.end();

        if (savedSSID == "") {
            Serial.println("[CAPTIVE] No Wi-Fi credentials found. Entering Setup Mode.");
            startPortal();
            return false;
        } else {
            Serial.println("[CAPTIVE] Connecting to Wi-Fi: " + savedSSID);
            WiFi.mode(WIFI_STA);
            WiFi.begin(savedSSID.c_str(), savedPASS.c_str());

            int attempts = 0;
            while (WiFi.status() != WL_CONNECTED && attempts < 30) {
                delay(500); Serial.print("."); attempts++;
            }

            if (WiFi.status() == WL_CONNECTED) {
                Serial.println("\n[CAPTIVE] Wi-Fi Connected. IP: " + WiFi.localIP().toString());
                            
                // --- INTEGRATE UNIQUE mDNS HERE ---
                // Use 'static' so the ESP32 keeps this in RAM forever and doesn't crash mDNS!
                static String mdns_hostname; 
                
                // Only generate the name if we haven't done it yet
                if (mdns_hostname == "") {
                    String mac = WiFi.macAddress();
                    mac.replace(":", ""); 
                    String uniqueSuffix = mac.substring(mac.length() - 4);
                    mdns_hostname = "otahub-" + uniqueSuffix;
                }
                
                MDNS.end(); // Clean up just in case
                
                if (MDNS.begin(mdns_hostname.c_str())) {
                    MDNS.addService("http", "tcp", 80); 
                    Serial.printf("[MDNS] Responder started! Access via http://%s.local\n", mdns_hostname.c_str());
                } else {
                    Serial.println("[MDNS] Error setting up responder!");
                }
                // ----------------------------------
                
                // RESTORED: Real Progress Endpoint
                server.on("/ota-status", HTTP_GET, [](AsyncWebServerRequest *request){
                    String json = "{\"progress\":" + String(OTA.otaProgress) + ",\"state\":" + String(OTA.otaState) + "}";
                    request->send(200, "application/json", json);
                });

                server.on("/", HTTP_GET, [currentVersion](AsyncWebServerRequest *request){
                    String html = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8"><meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>Smart ESP32 OTA Hub</title>
  <script src="https://cdn.tailwindcss.com"></script>
  <script src="https://unpkg.com/lucide@latest"></script>
  <link href="https://fonts.googleapis.com/css2?family=Inter:wght@300;400;500;600;700;800&display=swap" rel="stylesheet">
  <style>
    body { font-family: 'Inter', sans-serif; background-color: #0a0a0f; }
    .bg-mesh { position: fixed; inset: 0; z-index: -1; background: radial-gradient(circle 500px at 20% 20%, rgba(99,102,241,0.15), transparent), radial-gradient(circle 400px at 80% 70%, rgba(139,92,246,0.1), transparent); }
    @keyframes pulse-glow { 0%, 100% { box-shadow: 0 0 0 0 var(--tw-ring-color, rgba(99,102,241,0.4)); } 50% { box-shadow: 0 0 0 16px transparent; } }
    .animate-glow { animation: pulse-glow 2s infinite ease-in-out; }
  </style>
</head>
<body class="min-h-screen text-slate-200 flex items-center justify-center p-4 relative overflow-hidden">
  <div class="bg-mesh"></div>
  <div class="absolute inset-0 opacity-[0.02] pointer-events-none" style="background-image: radial-gradient(#fff 1px, transparent 0); background-size: 24px 24px;"></div>

  <div class="w-full max-w-md text-center z-10 space-y-8">
    <div>
      <div class="inline-flex items-center gap-2.5 px-4 py-1.5 rounded-full bg-indigo-500/10 border border-indigo-500/20 text-indigo-400 text-xs font-semibold tracking-wider uppercase mb-4">
        <i data-lucide="cpu" class="w-3.5 h-3.5 animate-pulse"></i> Firmware Hub
      </div>
      <h1 class="text-4xl font-extrabold tracking-tight text-white sm:text-5xl">OTA<span class="bg-gradient-to-r from-indigo-400 to-purple-500 bg-clip-text text-transparent">-Update</span></h1>
    </div>

    <div class="bg-white/[0.03] border border-white/5 rounded-3xl p-8 backdrop-blur-xl shadow-2xl space-y-8 relative">
      <div class="flex justify-center">
        <div id="status-ring" class="w-24 h-24 rounded-full bg-slate-900/60 border border-white/10 flex items-center justify-center transition-all duration-500">
          <div id="status-icon"><i data-lucide="loader-2" class="w-10 h-10 text-indigo-400 animate-spin"></i></div>
        </div>
      </div>

      <div class="space-y-2">
        <div id="status-text" class="text-2xl font-bold tracking-tight text-white">Checking Server...</div>
        <p id="status-desc" class="text-xs text-slate-400 tracking-wide">Querying remote server manifest file</p>
      </div>

      <div class="grid grid-cols-2 gap-3 pt-2">
        <div class="bg-white/[0.02] border border-white/5 rounded-2xl p-3.5 text-center">
          <span class="block text-[10px] uppercase font-bold tracking-widest text-slate-500 mb-1">Current Version</span>
          <span id="currentVersion" class="text-lg font-bold text-slate-300">--</span>
        </div>
        <div class="bg-white/[0.02] border border-white/5 rounded-2xl p-3.5">
          <span class="block text-[10px] uppercase font-bold tracking-widest text-slate-500 mb-2">Select Firmware Version</span>
          <select id="versionSelector" class="w-full bg-black/20 border border-white/10 rounded-2xl px-4 py-3 text-slate-200 focus:outline-none focus:border-indigo-500 focus:ring-1 focus:ring-indigo-500">
            <option value="">Loading versions...</option>
          </select>
        </div>
      </div>

      <div id="action" class="pt-2 min-h-[52px]">
        <div class="h-[52px] bg-white/[0.02] border border-white/5 rounded-full animate-pulse"></div>
      </div>
      
      <div class="pt-4 border-t border-white/5 mt-6">
        <button onclick="if(confirm('Disconnect Wi-Fi and reboot to Setup Mode?')) { fetch('/reset-wifi').catch(()=>{}); alert('Rebooting... Look for the Setup Network.'); }" class="w-full flex items-center justify-center gap-2 py-2.5 px-6 rounded-full bg-rose-500/5 hover:bg-rose-500/20 text-rose-400 text-sm font-semibold transition-all">
          <i data-lucide="wifi-off" class="w-4 h-4"></i> Factory Reset Wi-Fi
        </button>
      </div>
    </div>
  </div>

  <script>
    const CURRENT_VERSION = "%VERSION%"; 
    const JSON_URL = "https://raw.githubusercontent.com/segestic/OTA-Demo/main/manifest.json";

    const ringEl = document.getElementById("status-ring");
    const iconEl = document.getElementById("status-icon");
    const textEl = document.getElementById("status-text");
    const descEl = document.getElementById("status-desc");
    const versionSelector = document.getElementById("versionSelector");
    const actionEl = document.getElementById("action");

    document.getElementById("currentVersion").textContent = CURRENT_VERSION;

    async function checkFirmwareRegistry() {
      try {
        const response = await fetch(JSON_URL, { cache: "no-store" });
        if (!response.ok) throw new Error();
        const data = await response.json();

        versionSelector.innerHTML = '<option value="">Select a version...</option>';
        data.Configurations.forEach(config => {
          const option = document.createElement('option');
          option.value = config.Config;
          option.textContent = `Version ${config.Version}${config.Version === CURRENT_VERSION ? ' (Current)' : ''}`;
          versionSelector.appendChild(option);
        });

        renderActionReady();
      } catch (err) {
        renderSystemError();
      } finally { lucide.createIcons(); }
    }

    function startUpdate() {
      const selectedConfig = versionSelector.value;
      if (!selectedConfig) return alert('Please select a version');

      // Send the specific target to the ESP32
      fetch('/update?target=' + encodeURIComponent(selectedConfig)).catch(() => {});

      // Build real progress UI
      actionEl.innerHTML = `
        <div class="w-full space-y-3 mt-2">
          <div class="w-full bg-black/40 rounded-full h-4 border border-white/10 overflow-hidden relative shadow-inner">
            <div id="pb-fill" class="bg-gradient-to-r from-indigo-500 to-purple-500 h-full w-0 transition-all duration-300 ease-out relative">
              <div class="absolute inset-0 bg-white/20 animate-pulse"></div>
            </div>
          </div>
          <div class="flex justify-between items-center px-1">
            <span id="pb-text" class="text-xs text-indigo-300 font-bold tracking-wider uppercase animate-pulse">Connecting...</span>
            <span id="pb-pct" class="text-xs text-slate-400 font-bold">0%</span>
          </div>
        </div>
      `;

      let hasStarted = false;

      // Poll real device status
      const poll = setInterval(async () => {
        try {
          const res = await fetch('/ota-status', { cache: 'no-store' });
          const data = await res.json();
          
          if(data.state === 1) { // Downloading
            hasStarted = true;
            document.getElementById('pb-fill').style.width = data.progress + '%';
            document.getElementById('pb-pct').textContent = data.progress + '%';
            document.getElementById('pb-text').textContent = 'Downloading Firmware...';
            
            // If it hits 100%, the ESP is verifying and about to reboot
            if(data.progress >= 100) {
              clearInterval(poll);
              document.getElementById('pb-text').textContent = 'Verifying & Rebooting...';
              checkIfOnline();
            }
          } else if(data.state === 2) { // Failed
            clearInterval(poll);
            document.getElementById('pb-text').textContent = 'Update Failed';
            document.getElementById('pb-fill').className = "bg-rose-500 h-full transition-all";
          }
        } catch(e) {
          // If we lose connection mid-download or after 100%, device is rebooting
          clearInterval(poll);
          if (hasStarted) {
            document.getElementById('pb-text').textContent = 'Device Rebooting...';
            document.getElementById('pb-fill').style.width = '100%';
            checkIfOnline();
          } else {
            document.getElementById('pb-text').textContent = 'Connection Error';
            document.getElementById('pb-fill').className = "bg-rose-500 h-full transition-all";
          }
        }
      }, 500);
    }

    function renderActionReady() {
      ringEl.className = "w-24 h-24 rounded-full bg-indigo-500/10 border border-indigo-500/30 flex items-center justify-center animate-glow";
      ringEl.style.setProperty('--tw-ring-color', 'rgba(99, 102, 241, 0.35)');
      iconEl.innerHTML = `<i data-lucide="code-2" class="w-10 h-10 text-indigo-400"></i>`;
      textEl.textContent = "Ready to Flash";
      textEl.className = "text-2xl font-bold tracking-tight text-indigo-400";
      descEl.textContent = "Select a version above and initialize.";
      actionEl.innerHTML = `
        <button onclick="startUpdate()" class="w-full flex items-center justify-center gap-2 py-3.5 px-6 rounded-full bg-gradient-to-r from-indigo-500 to-purple-600 hover:from-indigo-600 hover:to-purple-700 text-white font-semibold transition-all duration-300 transform hover:-translate-y-0.5 hover:shadow-lg hover:shadow-indigo-500/20 active:translate-y-0">
          <i data-lucide="download-cloud" class="w-5 h-5"></i> Flash Selected Version
        </button>`;
    }

    function checkIfOnline() {
      // Ping the root URL every 3 seconds until we get a 200 OK
      const pingInterval = setInterval(async () => {
        try {
          const controller = new AbortController();
          const timeoutId = setTimeout(() => controller.abort(), 2000);
          
          const res = await fetch('/?ping=' + Date.now(), { 
            method: 'GET', 
            cache: 'no-store',
            signal: controller.signal
          });
          
          clearTimeout(timeoutId);

          if (res.ok) {
            clearInterval(pingInterval);
            document.getElementById('pb-fill').style.width = '100%';
            document.getElementById('pb-pct').textContent = '100%';
            document.getElementById('pb-text').textContent = 'Update Successful!';
            document.getElementById('pb-fill').className = "bg-emerald-500 h-full transition-all";
            
            setTimeout(() => window.location.reload(), 1500);
          }
        } catch(e) {
          // Device still offline, wait...
        }
      }, 3000);
    }

    function renderUpdateAvailable(version) {
      ringEl.className = "w-24 h-24 rounded-full bg-amber-500/10 border border-amber-500/30 flex items-center justify-center animate-glow";
      ringEl.style.setProperty('--tw-ring-color', 'rgba(245, 158, 11, 0.3)');
      iconEl.innerHTML = `<i data-lucide="arrow-up-circle" class="w-10 h-10 text-amber-400"></i>`;
      textEl.textContent = "Updates Available 🚀";
      textEl.className = "text-2xl font-bold tracking-tight text-amber-400";
      descEl.textContent = `New binary version ${version} discovered.`;
      actionEl.innerHTML = `
        <button onclick="startUpdate()" class="w-full flex items-center justify-center gap-2 py-3.5 px-6 rounded-full bg-gradient-to-r from-indigo-500 to-purple-600 hover:from-indigo-600 hover:to-purple-700 text-white font-semibold transition-all duration-300 transform hover:-translate-y-0.5 hover:shadow-lg hover:shadow-indigo-500/20 active:translate-y-0">
          <i data-lucide="download-cloud" class="w-5 h-5"></i> Flash Update Now
        </button>`;
    }

    function renderSystemUpToDate() {
      ringEl.className = "w-24 h-24 rounded-full bg-emerald-500/10 border border-emerald-500/30 flex items-center justify-center animate-glow";
      ringEl.style.setProperty('--tw-ring-color', 'rgba(16, 185, 129, 0.3)');
      iconEl.innerHTML = `<i data-lucide="check-circle-2" class="w-10 h-10 text-emerald-400"></i>`;
      textEl.textContent = "Current firmware installed ✅";
      textEl.className = "text-2xl font-bold tracking-tight text-emerald-400";
      descEl.textContent = "Select another version to upgrade or downgrade.";
      actionEl.innerHTML = `
        <button onclick="startUpdate()" class="w-full flex items-center justify-center gap-2 py-3.5 px-6 rounded-full bg-gradient-to-r from-indigo-500 to-purple-600 hover:from-indigo-600 hover:to-purple-700 text-white font-semibold transition-all duration-300 transform hover:-translate-y-0.5 hover:shadow-lg hover:shadow-indigo-500/20 active:translate-y-0">
          <i data-lucide="download-cloud" class="w-5 h-5"></i> Flash Selected Version
        </button>`;
    }

    function renderSystemError() {
      ringEl.className = "w-24 h-24 rounded-full bg-rose-500/10 border border-rose-500/30 flex items-center justify-center";
      iconEl.innerHTML = `<i data-lucide="alert-triangle" class="w-10 h-10 text-rose-400"></i>`;
      textEl.textContent = "Manifest Offline ❌";
      textEl.className = "text-2xl font-bold tracking-tight text-rose-400";
      descEl.textContent = "Failed to establish validation handshake with Git server.";
      actionEl.innerHTML = `
        <button onclick="window.location.reload()" class="w-full flex items-center justify-center gap-2 py-3.5 px-6 rounded-full bg-white/5 border border-white/10 text-white font-semibold hover:bg-white/10 transition-all duration-200">
          <i data-lucide="refresh-cw" class="w-4 h-4"></i> Retry Connection
        </button>`;
    }

    lucide.createIcons();
    setTimeout(checkFirmwareRegistry, 900);
  </script>
</body>
</html>
)rawliteral";
                    
                    html.replace("%VERSION%", currentVersion);
                    request->send(200, "text/html", html);
                });

                server.on("/update", HTTP_GET, [](AsyncWebServerRequest *request){
                    if (request->hasParam("target")) {
                        requestedOtaVersion = request->getParam("target")->value();
                        triggerOtaCheck = true;
                        request->send(200, "text/plain", "OK");
                    } else {
                        request->send(400, "text/plain", "Missing target");
                    }
                });

                server.on("/reset-wifi", HTTP_GET, [this](AsyncWebServerRequest *request){
                    request->send(200, "text/plain", "Resetting...");
                    this->pendingReset = true;
                    this->resetTriggerTime = millis();
                });

                server.begin();
                Serial.println("[CAPTIVE] Commercial Dashboard Started.");
                return true; 
            } else {
                Serial.println("\n[CAPTIVE] Wi-Fi connection failed! Falling back to setup.");
                WiFi.disconnect();
                startPortal();
                return false;
            }
        }
    }

    void handle() {
        if (portalActive) {
            dnsServer.processNextRequest();
        }

        if (pendingSave && (millis() - saveTriggerTime > 1000)) {
            pendingSave = false; 
            dnsServer.stop();
            
            Preferences prefs;
            prefs.begin("wifi_creds", false);
            prefs.putString("ssid", pendingSSID);
            prefs.putString("pass", pendingPASS);
            prefs.end();

            Serial.println("[CAPTIVE] Saved. Rebooting cleanly...");
            OTA.setIntentionalReboot(); 
            delay(500);
            ESP.restart(); 
        }

        if (pendingReset && (millis() - resetTriggerTime > 1000)) {
            pendingReset = false;
            
            Preferences prefs;
            prefs.begin("wifi_creds", false);
            prefs.clear(); 
            prefs.end();

            Serial.println("[CAPTIVE] Wi-Fi Credentials wiped! Rebooting cleanly...");
            OTA.setIntentionalReboot(); 
            delay(500);
            ESP.restart();
        }
    }
};
