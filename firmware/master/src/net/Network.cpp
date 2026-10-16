#include "net/Network.h"

#include <ESPmDNS.h>
#include <WiFi.h>
#include <WiFiManager.h>

#include "app/App.h"

static const char* const PORTAL_SSID = "SplitFlap-Setup";
static const char* const HOSTNAME = "splitflap";
static const int BOOT_BUTTON = 0;  // GPIO0, the "BOOT" button on ESP32 DevKit boards

static App* g_app = nullptr;

// Show a message on the flaps while blocked in the portal (the main loop isn't running yet).
static void showBlocking(const char* text, uint32_t maxMs) {
  g_app->setOverlay(text);
  uint32_t start = millis();
  do {
    g_app->loop();
    delay(10);
  } while (g_app->bus().busy() && millis() - start < maxMs);
}

void Network::begin(App& app) {
  g_app = &app;
  pinMode(BOOT_BUTTON, INPUT_PULLUP);
  bool buttonHeld = digitalRead(BOOT_BUTTON) == LOW;

  WiFi.mode(WIFI_STA);
  WiFi.setHostname(HOSTNAME);

  WiFiManager wm;
  WiFiManagerParameter adminKey("admin_key", "Admin key (8-64 chars, needed to change settings)", "", 64,
                                "type='password' minlength='8'");
  wm.addParameter(&adminKey);
  wm.setConfigPortalTimeout(300);
  wm.setAPCallback([](WiFiManager*) {
    Serial.printf("[wifi] setup portal open: join Wi-Fi \"%s\" and browse to 192.168.4.1\n", PORTAL_SSID);
    showBlocking("SETUP", 20000);
  });

  // The portal is an open AP, so it may only change the admin key when someone has physical access
  // (BOOT held) or on first setup. A Wi-Fi outage must never open it: otherwise anyone in range
  // could set a new admin key and take over the device.
  bool setupAllowed = buttonHeld || !app.adminKeySet();
  bool connected;
  if (setupAllowed) {
    Serial.println(buttonHeld ? "[wifi] BOOT held: opening setup portal" : "[wifi] no admin key yet: opening setup portal");
    connected = wm.startConfigPortal(PORTAL_SSID);
  } else {
    wm.setEnableConfigPortal(false);  // saved network unreachable -> retry in the background, no portal
    connected = wm.autoConnect(PORTAL_SSID);
  }

  app.setOverlay("");
  String key = adminKey.getValue();
  if (setupAllowed && key.length() >= 8) {
    app.setAdminKey(key.c_str());
    Serial.println("[wifi] admin key saved");
  } else if (!app.adminKeySet()) {
    Serial.println("[wifi] WARNING: no admin key set - settings can't be changed. Hold BOOT at power-on to set one.");
  }

  if (!connected) {
    Serial.println("[wifi] not connected (portal timed out); retrying in the background");
    WiFi.setAutoReconnect(true);
    WiFi.begin();
    return;
  }
  WiFi.setAutoReconnect(true);
  Serial.printf("[wifi] connected to %s, IP %s\n", WiFi.SSID().c_str(), WiFi.localIP().toString().c_str());
  if (MDNS.begin(HOSTNAME)) {
    MDNS.addService("http", "tcp", 80);
    Serial.printf("[wifi] http://%s.local/\n", HOSTNAME);
  }
}
