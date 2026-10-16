// I2C driver for the chain of split-flap units (protocol: firmware/PROTOCOL.md).
// Non-blocking: show() queues a frame, update() (call every loop) waits for all units to be
// idle and then sends it. Unlike upstream, waiting has a timeout instead of looping forever.
#pragma once

#include <Arduino.h>
#include <string>

#include "protocol.h"

class FlapBus {
 public:
  enum class State { Idle, WaitingForUnits, Moving };

  void begin(uint8_t units, int sda = 21, int scl = 22, uint32_t freq = 50000);  // also scans
  void setUnits(uint8_t units);  // change unit count at runtime (rescans the bus)

  // Probes every configured unit address; returns bitmask of units that ACKed (bit i = unit i).
  uint16_t scan();
  uint16_t presentMask() const { return present_; }
  uint8_t units() const { return units_; }

  // 1 = moving, 0 = idle, -1 = no answer (an empty write is sent to wake a sleeping unit).
  int isMoving(uint8_t unit);
  void send(uint8_t unit, uint8_t flapIndex, uint8_t rpm, bool rehome = false);

  // Queue a frame (one char per unit, already folded to flap glyphs; unknown chars show blank).
  // A newer frame replaces a queued one that has not been sent yet.
  void show(const std::string& frame, uint8_t rpm);
  // Re-home every unit and re-show the current frame.
  void rehomeAll();
  void update();

  bool busy() const { return state_ != State::Idle || pending_; }
  const std::string& shown() const { return shown_; }
  const String& lastError() const { return lastError_; }

  uint32_t waitTimeoutMs = 30000;  // max wait for all units to report idle
  uint32_t pollIntervalMs = 250;

 private:
  bool allIdle();
  void transmit();

  uint8_t units_ = 0;
  uint16_t present_ = 0;
  State state_ = State::Idle;
  bool pending_ = false;
  bool rehome_ = false;
  std::string queued_;
  std::string shown_;
  uint8_t rpm_ = 10;
  uint32_t waitStart_ = 0;
  uint32_t lastPoll_ = 0;
  String lastError_;
};
