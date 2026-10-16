// Split-flap social follower counter — ESP32 master.
#include <Arduino.h>

#include "charset.h"
#include "display/FlapBus.h"

static const uint8_t UNITS = 9;
static FlapBus bus;

// Temporary bring-up helper: type a line in the serial monitor to show it on the units.
static void handleSerial() {
  static String line;
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\r') continue;
    if (c != '\n') {
      if (line.length() < 64) line += c;
      continue;
    }
    if (line == "!scan") {
      bus.scan();
    } else if (line == "!rehome") {
      bus.rehomeAll();
    } else {
      char folded[UNIT_MAX_COUNT + 1];
      foldToFlaps(line.c_str(), folded, sizeof folded);
      bus.show(folded, 10);
    }
    line = "";
  }
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("\n[boot] split-flap master");
  bus.begin(UNITS);
  bus.scan();
}

void loop() {
  handleSerial();
  bus.update();
}
