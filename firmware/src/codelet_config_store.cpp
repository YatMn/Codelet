#include "codelet_config_store.h"

#ifndef CODELET_NATIVE_TEST
#include <Preferences.h>
#include <string.h>
#endif

namespace {

constexpr const char *kNamespace = "codelet_cfg";
constexpr const char *kWifiSsid = "wifi_ssid";
constexpr const char *kWifiPassword = "wifi_pass";
constexpr const char *kAgentHost = "agent_host";
constexpr const char *kAgentPort = "agent_port";
constexpr const char *kApiToken = "api_token";
constexpr const char *kConfigVersion = "cfg_version";

bool boundedStringLength(const char *value, size_t capacity, size_t &length) {
  if (value == nullptr) {
    return false;
  }

  for (size_t index = 0; index < capacity; index++) {
    if (value[index] == '\0') {
      length = index;
      return true;
    }
  }
  return false;
}

#ifndef CODELET_NATIVE_TEST
void readString(Preferences &preferences, const char *key, char *buffer, size_t bufferSize) {
  if (buffer == nullptr || bufferSize == 0) {
    return;
  }
  String value = preferences.getString(key, "");
  strncpy(buffer, value.c_str(), bufferSize - 1);
  buffer[bufferSize - 1] = '\0';
}
#endif

}  // namespace

bool hasValidRuntimeConfig(RuntimeConfigStore &store) {
  RuntimeConfig config = defaultRuntimeConfig();
  if (!store.load(config)) {
    return false;
  }
  return validateRuntimeConfig(config).valid;
}

#ifndef CODELET_NATIVE_TEST
bool PreferencesRuntimeConfigStore::load(RuntimeConfig &config) {
  Preferences preferences;
  if (!preferences.begin(kNamespace, true)) {
    return false;
  }

  config = defaultRuntimeConfig();
  config.configVersion = preferences.getUShort(kConfigVersion, 0);
  readString(preferences, kWifiSsid, config.wifiSsid, sizeof(config.wifiSsid));
  readString(preferences, kWifiPassword, config.wifiPassword, sizeof(config.wifiPassword));
  readString(preferences, kAgentHost, config.agentHost, sizeof(config.agentHost));
  config.agentPort = preferences.getUShort(kAgentPort, 0);
  readString(preferences, kApiToken, config.apiToken, sizeof(config.apiToken));
  preferences.end();
  return config.configVersion != 0;
}

bool PreferencesRuntimeConfigStore::save(const RuntimeConfig &config) {
  if (!validateRuntimeConfig(config).valid) {
    return false;
  }
  size_t tokenLength = 0;
  if (!boundedStringLength(config.apiToken, sizeof(config.apiToken), tokenLength)) {
    return false;
  }

  Preferences preferences;
  if (!preferences.begin(kNamespace, false)) {
    return false;
  }

  bool ok = true;
  ok = ok && preferences.putUShort(kConfigVersion, config.configVersion) > 0;
  ok = ok && preferences.putString(kWifiSsid, config.wifiSsid) > 0;
  ok = ok && preferences.putString(kWifiPassword, config.wifiPassword) > 0;
  ok = ok && preferences.putString(kAgentHost, config.agentHost) > 0;
  ok = ok && preferences.putUShort(kAgentPort, config.agentPort) > 0;
  if (tokenLength == 0) {
    ok = ok && (!preferences.isKey(kApiToken) || preferences.remove(kApiToken));
  } else {
    ok = ok && preferences.putString(kApiToken, config.apiToken) == tokenLength;
  }
  preferences.end();
  return ok;
}

bool PreferencesRuntimeConfigStore::clear() {
  Preferences preferences;
  if (!preferences.begin(kNamespace, false)) {
    return false;
  }
  bool ok = preferences.clear();
  preferences.end();
  return ok;
}
#endif
