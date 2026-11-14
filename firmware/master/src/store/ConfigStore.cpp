#include "store/ConfigStore.h"

#include <Preferences.h>

namespace ConfigStore {

// The lists are stored as blobs: an NVS string holds at most 4000 bytes, a full word list can be longer.
static std::string readBlob(Preferences& prefs, const char* key) {
  size_t len = prefs.isKey(key) ? prefs.getBytesLength(key) : 0;
  std::string out(len, '\0');
  if (len) prefs.getBytes(key, &out[0], len);
  return out;
}

static void apply(Config& c, const std::string& json, const char* what) {
  if (json.empty()) return;
  std::string err = applyConfigJson(c, json);
  if (!err.empty()) Serial.printf("[config] stored %s rejected (%s), using defaults\n", what, err.c_str());
}

Config load() {
  Config c;
  Preferences prefs;
  if (prefs.begin("config", true)) {
    String json = prefs.getString("json", "");
    std::string words = readBlob(prefs, "words"), presets = readBlob(prefs, "presets");
    prefs.end();
    apply(c, json.c_str(), "config");
    apply(c, words, "word list");
    apply(c, presets, "saved messages");
  }
  return c;
}

void save(const Config& c, const Config* before) {
  Preferences prefs;
  prefs.begin("config", false);
  prefs.putString("json", configToJson(c, true, false).c_str());
  if (!before || before->words != c.words) {
    std::string words = wordsToJson(c);
    prefs.putBytes("words", words.data(), words.size());
  }
  if (!before || before->presets != c.presets) {
    std::string presets = presetsToJson(c);
    prefs.putBytes("presets", presets.data(), presets.size());
  }
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
