// Pure timing/policy decisions. No Arduino dependencies: unit-tested in env:native.
// Times are Unix epoch seconds (from NTP); 0 = unknown.
#pragma once

#include <stdint.h>

constexpr uint32_t HOUR_S = 3600;
constexpr uint32_t DAY_S = 24 * HOUR_S;

// Minutes until the next fetch: pollMinutes when healthy, else 15 -> 30 -> 60 -> 120 (cap),
// and never sooner than an HTTP Retry-After.
uint32_t backoffMinutes(uint8_t consecutiveFailures, uint32_t pollMinutes, uint32_t retryAfterSec = 0);

// Instagram long-lived token (~60 days): refresh once it is >= 30 days old. Meta only allows refresh
// of tokens >= 24 h old that have not expired yet.
bool instagramNeedsRefresh(uint64_t now, uint64_t issuedAt, uint64_t expiresAt);

// TikTok access token (~24 h): refresh when < 2 h remain (or already expired).
bool tiktokNeedsRefresh(uint64_t now, uint64_t accessExpiresAt);

// Next enabled platform after `current` (bit i of enabledMask = platform i), wrapping around.
// Returns -1 if none is enabled.
int nextPlatform(int current, uint8_t enabledMask, uint8_t platformCount);

// True if minuteOfDay (0..1439) is inside [start, end); handles ranges across midnight.
// start == end means quiet hours are disabled.
bool inQuietHours(uint16_t minuteOfDay, uint16_t startMinute, uint16_t endMinute);

// True when the count crosses a multiple of `step` upwards (prev > 0 so boot doesn't celebrate).
bool crossedMilestone(uint64_t previous, uint64_t current, uint64_t step);
