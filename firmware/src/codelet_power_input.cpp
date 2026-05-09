#include "codelet_power_input.h"

#include "codelet_config.h"

bool isShutdownTouchRegion(int16_t x, int16_t y) {
  return x >= 800 && x < static_cast<int16_t>(CODELET_SCREEN_WIDTH) &&
         y >= 0 && y < 160;
}

bool ShutdownHoldTracker::update(PowerTouchSample sample, uint32_t nowMs) {
  if (!sample.pressed || !isShutdownTouchRegion(sample.x, sample.y)) {
    reset();
    return false;
  }

  if (!holding_) {
    holding_ = true;
    triggered_ = false;
    holdStartMs_ = nowMs;
    return false;
  }

  if (!triggered_ && nowMs - holdStartMs_ >= CODELET_SHUTDOWN_HOLD_MS) {
    triggered_ = true;
    return true;
  }

  return false;
}

void ShutdownHoldTracker::reset() {
  holdStartMs_ = 0;
  holding_ = false;
  triggered_ = false;
}
