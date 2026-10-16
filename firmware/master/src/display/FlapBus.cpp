#include "display/FlapBus.h"

#include <Wire.h>

#include "charset.h"

static uint8_t addressOf(uint8_t unit) { return UNIT_I2C_BASE + unit; }

void FlapBus::begin(uint8_t units, int sda, int scl, uint32_t freq) {
  units_ = units > UNIT_MAX_COUNT ? UNIT_MAX_COUNT : units;
  Wire.begin(sda, scl, freq);
  Wire.setTimeOut(50);
  shown_.assign(units_, '?');  // unknown until the first frame is sent
}

uint16_t FlapBus::scan() {
  present_ = 0;
  for (uint8_t i = 0; i < units_; i++) {
    // Units sleep in AVR power-down but wake on address match, so retry once.
    for (int attempt = 0; attempt < 2; attempt++) {
      Wire.beginTransmission(addressOf(i));
      if (Wire.endTransmission() == 0) {
        present_ |= (1u << i);
        break;
      }
      delay(5);
    }
  }
  Serial.printf("[bus] scan: %u/%u units answered:", __builtin_popcount(present_), units_);
  for (uint8_t i = 0; i < units_; i++) {
    Serial.printf(" 0x%02X=%s", addressOf(i), (present_ & (1u << i)) ? "ok" : "MISSING");
  }
  Serial.println();
  return present_;
}

int FlapBus::isMoving(uint8_t unit) {
  uint8_t addr = addressOf(unit);
  if (Wire.requestFrom(addr, (uint8_t)UNIT_ANSWER_SIZE) == UNIT_ANSWER_SIZE) {
    return Wire.read() ? 1 : 0;
  }
  // No answer: like upstream, poke the unit so it wakes up for the next poll.
  Wire.beginTransmission(addr);
  Wire.endTransmission();
  return -1;
}

void FlapBus::send(uint8_t unit, uint8_t flapIndex, uint8_t rpm, bool rehome) {
  Wire.beginTransmission(addressOf(unit));
  Wire.write(flapIndex);
  Wire.write(rpm);
  if (rehome) Wire.write((uint8_t)CMD_REHOME);
  uint8_t err = Wire.endTransmission();
  if (err != 0) {
    lastError_ = String("unit ") + unit + " (0x" + String(addressOf(unit), HEX) + ") write failed, err " + err;
    Serial.printf("[bus] %s\n", lastError_.c_str());
  }
}

void FlapBus::show(const std::string& frame, uint8_t rpm) {
  queued_ = frame;
  queued_.resize(units_, ' ');
  rpm_ = constrain(rpm, FLAP_SPEED_MIN, FLAP_SPEED_MAX);
  pending_ = true;
}

void FlapBus::rehomeAll() {
  rehome_ = true;
  if (!pending_) {
    queued_ = shown_;
    for (char& c : queued_) {
      if (c == '?') c = ' ';
    }
    pending_ = true;
  }
}

bool FlapBus::allIdle() {
  for (uint8_t i = 0; i < units_; i++) {
    if (!(present_ & (1u << i))) continue;  // missing at boot: don't wait for it forever
    if (isMoving(i) != 0) return false;
  }
  return true;
}

void FlapBus::transmit() {
  for (uint8_t i = 0; i < units_; i++) {
    int8_t idx = flapIndex(queued_[i]);
    send(i, idx < 0 ? 0 : (uint8_t)idx, rpm_, rehome_);
  }
  Serial.printf("[bus] show \"%s\" @%u rpm%s\n", queued_.c_str(), rpm_, rehome_ ? " (re-home)" : "");
  shown_ = queued_;
  pending_ = false;
  rehome_ = false;
}

void FlapBus::update() {
  uint32_t now = millis();
  switch (state_) {
    case State::Idle:
      if (!pending_) return;
      state_ = State::WaitingForUnits;
      waitStart_ = now;
      lastPoll_ = now - pollIntervalMs;  // poll immediately
      // fallthrough
    case State::WaitingForUnits:
    case State::Moving:
      if (now - lastPoll_ < pollIntervalMs) return;
      lastPoll_ = now;
      if (allIdle()) {
        if (pending_) {
          transmit();
          state_ = State::Moving;
          waitStart_ = now;
          lastPoll_ = now;  // give units a moment to start before polling
        } else {
          state_ = State::Idle;
        }
      } else if (now - waitStart_ > waitTimeoutMs) {
        lastError_ = "timeout waiting for units to stop";
        Serial.printf("[bus] %s after %lu ms\n", lastError_.c_str(), (unsigned long)waitTimeoutMs);
        if (pending_) {
          transmit();  // units accept a new target at any time; don't stall the display forever
          state_ = State::Moving;
          waitStart_ = now;
        } else {
          state_ = State::Idle;
        }
      }
      return;
  }
}
