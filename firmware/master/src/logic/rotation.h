// Decides what the display shows from moment to moment: which platform, which random word, which
// entry of the mix. Pure: no Arduino dependencies, unit-tested in env:native.
#pragma once

#include <stdint.h>

#include <string>
#include <vector>

#include "logic/config.h"

enum class SceneKind : uint8_t { Blank, Followers, Word, Text, Clock, Date, Weather, Countdown };

struct Scene {
  SceneKind kind = SceneKind::Blank;
  Platform platform = Platform::TikTok;  // Followers
  std::string word;                      // Word
};

class Rotation {
 public:
  using Rng = uint32_t (*)();  // uniform 32-bit random numbers
  explicit Rotation(Rng rng) : rng_(rng) {}

  // Call often. nowMs is a millisecond clock that may wrap.
  Scene update(const Config& c, uint32_t nowMs);

  // The word list was edited: forget the pending order. The word on show stays until its time is
  // up, unless it was removed.
  void wordsChanged();
  // Start the current mode from its beginning (the mix list was edited).
  void restart() { started_ = false; }
  // Cycle mode: show this platform now and give it a full turn.
  void jumpTo(Platform p, uint32_t nowMs);

 private:
  bool nextWord(const Config& c);
  bool enterMixItem(const Config& c);
  Scene mixScene(const Config& c) const;

  Rng rng_;
  bool started_ = false;
  DisplayMode mode_ = DisplayMode::Off;
  uint32_t slotStartMs_ = 0;
  int platform_ = -1;      // platform on show in Cycle or a Followers mix entry
  int item_ = -1;          // mix entry on show
  uint8_t platformsLeft_ = 0;  // platforms still to show in this Followers mix entry
  std::vector<uint8_t> bag_;   // word indices not shown yet, in the order they will show
  std::string word_;
  bool haveWord_ = false;
  bool checkWord_ = false;
};
