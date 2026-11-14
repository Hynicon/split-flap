// Settings and last-good-count cache in NVS.
#pragma once

#include <Arduino.h>

#include "logic/config.h"

namespace ConfigStore {
Config load();
// The word list and the saved messages have their own keys. Pass the settings as they were before
// the change to skip rewriting the lists that did not change.
void save(const Config& c, const Config* before = nullptr);

// Last good follower count per platform, so a reboot shows the old number instead of blanks.
bool loadCount(Platform p, uint64_t& count, uint64_t& atEpoch);
void saveCount(Platform p, uint64_t count, uint64_t atEpoch);
}  // namespace ConfigStore
