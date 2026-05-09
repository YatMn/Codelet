#pragma once

#include <stdint.h>

enum class CodeletResetReason : uint8_t {
  Unknown,
  PowerOn,
  External,
  Software,
  Panic,
  Watchdog,
  DeepSleep,
  Brownout,
  Usb,
  Jtag,
  PowerGlitch,
};

struct BootPowerIntentDecision {
  bool shutdownRequested;
  const char *reason;
};

class PowerIntentStore {
 public:
  virtual ~PowerIntentStore() = default;
  virtual bool loadShutdownArmed(bool &armed) = 0;
  virtual bool saveShutdownArmed(bool armed) = 0;
  virtual bool clearShutdownArmed() = 0;
};

bool resetReasonCanRepresentPhysicalPowerPress(CodeletResetReason reason);
BootPowerIntentDecision decideBootPowerIntent(bool shutdownArmed, CodeletResetReason reason);
const char *resetReasonName(CodeletResetReason reason);

#ifndef CODELET_NATIVE_TEST
class PreferencesPowerIntentStore : public PowerIntentStore {
 public:
  bool loadShutdownArmed(bool &armed) override;
  bool saveShutdownArmed(bool armed) override;
  bool clearShutdownArmed() override;
};
#endif
