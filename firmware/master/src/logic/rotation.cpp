#include "logic/rotation.h"

#include <algorithm>

#include "logic/policy.h"

static bool enabled(const Config& c, int platform) {
  return platform >= 0 && platform < PLATFORM_COUNT && ((c.enabledMask >> platform) & 1);
}

static uint8_t enabledCount(const Config& c) {
  uint8_t n = 0;
  for (uint8_t i = 0; i < PLATFORM_COUNT; i++) n += (c.enabledMask >> i) & 1;
  return n;
}

void Rotation::wordsChanged() {
  bag_.clear();
  checkWord_ = true;
}

void Rotation::jumpTo(Platform p, uint32_t nowMs) {
  platform_ = (int)p;
  slotStartMs_ = nowMs;
}

// Shuffle bag: every word shows once before any repeats, and a refill never starts with the
// word that just showed.
bool Rotation::nextWord(const Config& c) {
  const size_t n = c.words.size();
  if (n == 0) {
    haveWord_ = false;
    return false;
  }
  while (!bag_.empty() && bag_.back() >= n) bag_.pop_back();
  if (bag_.empty()) {
    for (size_t i = 0; i < n; i++) bag_.push_back((uint8_t)i);
    for (size_t i = n - 1; i > 0; i--) std::swap(bag_[i], bag_[rng_() % (i + 1)]);
    if (n > 1 && haveWord_ && c.words[bag_.back()] == word_) std::swap(bag_.back(), bag_.front());
  }
  word_ = c.words[bag_.back()];
  bag_.pop_back();
  haveWord_ = true;
  return true;
}

// Sets up the mix entry item_ for showing. False if it has nothing to show.
bool Rotation::enterMixItem(const Config& c) {
  switch (c.mix[item_].kind) {
    case MixKind::Followers:
      platformsLeft_ = enabledCount(c);
      if (platformsLeft_ == 0) return false;
      platform_ = nextPlatform(platform_, c.enabledMask, PLATFORM_COUNT);
      platformsLeft_--;
      return true;
    case MixKind::Random:
      return nextWord(c);
    default:
      return true;
  }
}

Scene Rotation::mixScene(const Config& c) const {
  Scene s;
  switch (c.mix[item_].kind) {
    case MixKind::Followers:
      s.kind = SceneKind::Followers;
      s.platform = (Platform)platform_;
      break;
    case MixKind::Random:
      s.kind = SceneKind::Word;
      s.word = word_;
      break;
    case MixKind::Text: s.kind = SceneKind::Text; break;
    case MixKind::Clock: s.kind = SceneKind::Clock; break;
    case MixKind::Date: s.kind = SceneKind::Date; break;
    case MixKind::Weather: s.kind = SceneKind::Weather; break;
    case MixKind::Countdown: s.kind = SceneKind::Countdown; break;
  }
  return s;
}

Scene Rotation::update(const Config& c, uint32_t nowMs) {
  if (!started_ || c.mode != mode_) {
    started_ = true;
    mode_ = c.mode;
    item_ = -1;
    slotStartMs_ = nowMs;
  }
  if (checkWord_) {
    checkWord_ = false;
    if (haveWord_ && std::find(c.words.begin(), c.words.end(), word_) == c.words.end()) haveWord_ = false;
  }
  const uint32_t elapsed = nowMs - slotStartMs_;
  Scene s;

  switch (c.mode) {
    case DisplayMode::Off:
      return s;
    case DisplayMode::Text:
      s.kind = SceneKind::Text;
      return s;
    case DisplayMode::Clock:
      s.kind = SceneKind::Clock;
      return s;
    case DisplayMode::Date:
      s.kind = SceneKind::Date;
      return s;
    case DisplayMode::Weather:
      s.kind = SceneKind::Weather;
      return s;
    case DisplayMode::Countdown:
      s.kind = SceneKind::Countdown;
      return s;
    case DisplayMode::Single:
      s.kind = SceneKind::Followers;
      s.platform = c.single;
      return s;
    case DisplayMode::Cycle:
      if (!enabled(c, platform_) || elapsed >= c.cycleSeconds * 1000UL) {
        platform_ = nextPlatform(platform_, c.enabledMask, PLATFORM_COUNT);
        slotStartMs_ = nowMs;
      }
      if (platform_ < 0) return s;
      s.kind = SceneKind::Followers;
      s.platform = (Platform)platform_;
      return s;
    case DisplayMode::Random:
      if (!haveWord_ || elapsed >= c.randomSeconds * 1000UL) {
        nextWord(c);
        slotStartMs_ = nowMs;
      }
      if (!haveWord_) return s;
      s.kind = SceneKind::Word;
      s.word = word_;
      return s;
    case DisplayMode::Mix:
      break;
  }

  if (c.mix.empty()) return s;
  bool valid = item_ >= 0 && item_ < (int)c.mix.size();
  if (valid) {
    const MixItem& m = c.mix[item_];
    bool lost = (m.kind == MixKind::Followers && !enabled(c, platform_)) || (m.kind == MixKind::Random && !haveWord_);
    if (!lost && elapsed < m.seconds * 1000UL) return mixScene(c);
    // A Followers entry stays on until every enabled platform has had its turn.
    if (!lost && m.kind == MixKind::Followers && platformsLeft_ > 0) {
      platform_ = nextPlatform(platform_, c.enabledMask, PLATFORM_COUNT);
      platformsLeft_--;
      slotStartMs_ = nowMs;
      return mixScene(c);
    }
  }
  // Next entry that has something to show.
  for (size_t tries = 0; tries < c.mix.size(); tries++) {
    item_ = valid ? (item_ + 1) % (int)c.mix.size() : 0;
    valid = true;
    if (enterMixItem(c)) {
      slotStartMs_ = nowMs;
      return mixScene(c);
    }
  }
  item_ = -1;
  return s;
}
