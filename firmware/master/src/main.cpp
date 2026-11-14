// Split-flap social follower counter — ESP32 master.
#include <Arduino.h>

#include "app/App.h"
#include "net/Network.h"
#include "net/Remote.h"
#include "web/WebServer.h"

static App app;

// Serial console for bring-up: "!scan", "!rehome", "!status", or any text to show it.
static void handleSerial() {
  static String line;
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\r') continue;
    if (c != '\n') {
      if (line.length() < 80) line += c;
      continue;
    }
    if (line == "!scan") {
      app.bus().scan();
    } else if (line == "!rehome") {
      app.requestRehome();
    } else if (line == "!status") {
      Serial.println(app.statusJson().c_str());
    } else if (line.length()) {
      std::string err = app.showText(line.c_str(), "left");
      if (!err.empty()) Serial.printf("[serial] %s\n", err.c_str());
    }
    line = "";
  }
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.printf("\n[boot] split-flap master, reset reason %d\n", (int)esp_reset_reason());
  app.begin();
  Network::begin(app);  // may block in the setup portal
  Web::begin(app);
  Remote::begin(app);
}

void loop() {
  handleSerial();
  app.loop();
  delay(1);
}
