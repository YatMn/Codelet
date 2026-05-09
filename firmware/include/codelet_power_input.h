#pragma once

#include <stdint.h>

struct PowerTouchSample {
  int16_t x;
  int16_t y;
  bool pressed;
};

class ShutdownHoldTracker {
 public:
  bool update(PowerTouchSample sample, uint32_t nowMs);
  void reset();

 private:
  uint32_t holdStartMs_ = 0;
  bool holding_ = false;
  bool triggered_ = false;
};

bool isShutdownTouchRegion(int16_t x, int16_t y);
