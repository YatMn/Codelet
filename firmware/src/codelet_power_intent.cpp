#include "codelet_power_intent.h"

#ifndef CODELET_NATIVE_TEST
#include <Preferences.h>
#endif

namespace {

constexpr const char *kShutdownRequested = "physical_power_shutdown";
constexpr const char *kNoShutdown = "continue_boot";

#ifndef CODELET_NATIVE_TEST
constexpr const char *kNamespace = "codelet_power";
constexpr const char *kShutdownArmed = "shutdown_armed";
#endif

}  // namespace

bool resetReasonCanRepresentPhysicalPowerPress(CodeletResetReason reason) {
  return reason == CodeletResetReason::PowerOn || reason == CodeletResetReason::External;
}

BootPowerIntentDecision decideBootPowerIntent(bool shutdownArmed, CodeletResetReason reason) {
  if (shutdownArmed && resetReasonCanRepresentPhysicalPowerPress(reason)) {
    return {true, kShutdownRequested};
  }
  return {false, kNoShutdown};
}

const char *resetReasonName(CodeletResetReason reason) {
  switch (reason) {
    case CodeletResetReason::PowerOn: return "power_on";
    case CodeletResetReason::External: return "external";
    case CodeletResetReason::Software: return "software";
    case CodeletResetReason::Panic: return "panic";
    case CodeletResetReason::Watchdog: return "watchdog";
    case CodeletResetReason::DeepSleep: return "deep_sleep";
    case CodeletResetReason::Brownout: return "brownout";
    case CodeletResetReason::Usb: return "usb";
    case CodeletResetReason::Jtag: return "jtag";
    case CodeletResetReason::PowerGlitch: return "power_glitch";
    case CodeletResetReason::Unknown: return "unknown";
  }
  return "unknown";
}

#ifndef CODELET_NATIVE_TEST
bool PreferencesPowerIntentStore::loadShutdownArmed(bool &armed) {
  Preferences preferences;
  if (!preferences.begin(kNamespace, true)) {
    armed = false;
    return false;
  }
  armed = preferences.getBool(kShutdownArmed, false);
  preferences.end();
  return true;
}

bool PreferencesPowerIntentStore::saveShutdownArmed(bool armed) {
  Preferences preferences;
  if (!preferences.begin(kNamespace, false)) {
    return false;
  }
  bool ok = preferences.putBool(kShutdownArmed, armed) == 1;
  preferences.end();
  return ok;
}

bool PreferencesPowerIntentStore::clearShutdownArmed() {
  Preferences preferences;
  if (!preferences.begin(kNamespace, false)) {
    return false;
  }
  bool ok = !preferences.isKey(kShutdownArmed) || preferences.remove(kShutdownArmed);
  preferences.end();
  return ok;
}
#endif
