#include "logic/policy.h"

uint32_t backoffMinutes(uint8_t consecutiveFailures, uint32_t pollMinutes, uint32_t retryAfterSec) {
  uint32_t minutes = pollMinutes;
  if (consecutiveFailures > 0) {
    uint8_t shift = consecutiveFailures - 1 > 3 ? 3 : consecutiveFailures - 1;
    minutes = 15u << shift;  // 15, 30, 60, 120
  }
  uint32_t retryAfterMin = (retryAfterSec + 59) / 60;
  return minutes > retryAfterMin ? minutes : retryAfterMin;
}

bool instagramNeedsRefresh(uint64_t now, uint64_t issuedAt, uint64_t expiresAt) {
  if (now == 0 || issuedAt == 0) return false;  // clock not synced / unknown age
  if (expiresAt != 0 && now >= expiresAt) return false;  // too late: needs a new login
  return now >= issuedAt + 30ull * DAY_S;
}

bool tiktokNeedsRefresh(uint64_t now, uint64_t accessExpiresAt) {
  if (now == 0) return false;
  return accessExpiresAt == 0 || now + 2ull * HOUR_S >= accessExpiresAt;
}

int nextPlatform(int current, uint8_t enabledMask, uint8_t platformCount) {
  for (uint8_t step = 1; step <= platformCount; step++) {
    int candidate = (current + step + platformCount) % platformCount;
    if (enabledMask & (1u << candidate)) return candidate;
  }
  return -1;
}

bool inQuietHours(uint16_t minuteOfDay, uint16_t startMinute, uint16_t endMinute) {
  if (startMinute == endMinute) return false;
  if (startMinute < endMinute) return minuteOfDay >= startMinute && minuteOfDay < endMinute;
  return minuteOfDay >= startMinute || minuteOfDay < endMinute;  // e.g. 22:00 -> 07:00
}

bool crossedMilestone(uint64_t previous, uint64_t current, uint64_t step) {
  if (step == 0 || previous == 0) return false;
  return current / step > previous / step;
}
