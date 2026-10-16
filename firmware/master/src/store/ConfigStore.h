// Settings and last-good-count cache in NVS.
#pragma once

#include <Arduino.h>

#include "logic/config.h"

namespace ConfigStore {
Config load();
void save(const Config& c);

// Last good follower count per platform, so a reboot shows the old number instead of dashes.
bool loadCount(Platform p, uint64_t& count, uint64_t& atEpoch);
void saveCount(Platform p, uint64_t count, uint64_t atEpoch);
}  // namespace ConfigStore
