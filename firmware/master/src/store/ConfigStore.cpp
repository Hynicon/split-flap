#include "store/ConfigStore.h"

#include <Preferences.h>

namespace ConfigStore {

Config load() {
  Config c;
  Preferences prefs;
  if (prefs.begin("config", true)) {
    String json = prefs.getString("json", "");
    prefs.end();
    if (json.length()) {
      std::string err = applyConfigJson(c, json.c_str());
      if (!err.empty()) Serial.printf("[config] stored config rejected (%s), using defaults\n", err.c_str());
    }
  }
  return c;
}

void save(const Config& c) {
  Preferences prefs;
  prefs.begin("config", false);
  prefs.putString("json", configToJson(c, true).c_str());
  prefs.end();
}

bool loadCount(Platform p, uint64_t& count, uint64_t& atEpoch) {
  Preferences prefs;
  if (!prefs.begin("cache", true)) return false;
  String key = String("c") + (int)p;
  bool found = prefs.isKey(key.c_str());
  if (found) {
    count = prefs.getULong64(key.c_str(), 0);
    atEpoch = prefs.getULong64((String("t") + (int)p).c_str(), 0);
  }
  prefs.end();
  return found;
}

void saveCount(Platform p, uint64_t count, uint64_t atEpoch) {
  Preferences prefs;
  prefs.begin("cache", false);
  prefs.putULong64((String("c") + (int)p).c_str(), count);
  prefs.putULong64((String("t") + (int)p).c_str(), atEpoch);
  prefs.end();
}

}  // namespace ConfigStore
