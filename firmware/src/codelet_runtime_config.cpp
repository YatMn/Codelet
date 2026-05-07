#include "codelet_runtime_config.h"

#include <stdio.h>

namespace {

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

bool isEmptyOrUnterminated(const char *value, size_t capacity) {
  size_t length = 0;
  return !boundedStringLength(value, capacity, length) || length == 0;
}

bool parseIpv4Part(const char *start, const char *end) {
  if (start == end || end - start > 3) {
    return false;
  }

  int value = 0;
  for (const char *cursor = start; cursor < end; cursor++) {
    if (*cursor < '0' || *cursor > '9') {
      return false;
    }
    value = value * 10 + (*cursor - '0');
  }

  return value >= 0 && value <= 255;
}

bool isValidIpv4AddressBounded(const char *value, size_t capacity, size_t &length) {
  if (!boundedStringLength(value, capacity, length) || length == 0) {
    return false;
  }

  const char *partStart = value;
  int parts = 0;
  const char *end = value + length;
  for (const char *cursor = value; cursor <= end; cursor++) {
    if (cursor == end || *cursor == '.') {
      if (!parseIpv4Part(partStart, cursor)) {
        return false;
      }
      parts++;
      partStart = cursor + 1;
    }
  }

  return parts == 4;
}

}  // namespace

RuntimeConfig defaultRuntimeConfig() {
  RuntimeConfig config{};
  config.agentPort = 8765;
  config.configVersion = CODELET_CONFIG_VERSION;
  return config;
}

bool isValidIpv4Address(const char *value) {
  size_t length = 0;
  return isValidIpv4AddressBounded(value, CODELET_AGENT_HOST_LEN, length);
}

RuntimeConfigValidation validateRuntimeConfig(const RuntimeConfig &config) {
  if (config.configVersion != CODELET_CONFIG_VERSION) {
    return {false, RuntimeConfigError::UnsupportedVersion};
  }
  if (isEmptyOrUnterminated(config.wifiSsid, sizeof(config.wifiSsid))) {
    return {false, RuntimeConfigError::MissingWifiSsid};
  }
  if (isEmptyOrUnterminated(config.wifiPassword, sizeof(config.wifiPassword))) {
    return {false, RuntimeConfigError::MissingWifiPassword};
  }
  size_t hostLength = 0;
  if (!isValidIpv4AddressBounded(config.agentHost, sizeof(config.agentHost), hostLength)) {
    return {false, RuntimeConfigError::InvalidAgentHost};
  }
  if (config.agentPort == 0) {
    return {false, RuntimeConfigError::InvalidAgentPort};
  }
  size_t tokenLength = 0;
  if (!boundedStringLength(config.apiToken, sizeof(config.apiToken), tokenLength)) {
    return {false, RuntimeConfigError::InvalidApiToken};
  }
  return {true, RuntimeConfigError::None};
}

bool buildAgentBaseUrl(const RuntimeConfig &config, char *buffer, size_t bufferSize) {
  if (buffer == nullptr || bufferSize == 0) {
    return false;
  }

  size_t hostLength = 0;
  if (!isValidIpv4AddressBounded(config.agentHost, sizeof(config.agentHost), hostLength) ||
      config.agentPort == 0) {
    buffer[0] = '\0';
    return false;
  }

  int written = snprintf(buffer, bufferSize, "http://%.*s:%u", static_cast<int>(hostLength),
                         config.agentHost, config.agentPort);
  if (written < 0 || static_cast<size_t>(written) >= bufferSize) {
    buffer[0] = '\0';
    return false;
  }
  return true;
}

const char *runtimeConfigErrorLabel(RuntimeConfigError error) {
  switch (error) {
    case RuntimeConfigError::None:
      return "ok";
    case RuntimeConfigError::MissingWifiSsid:
      return "missing_wifi_ssid";
    case RuntimeConfigError::MissingWifiPassword:
      return "missing_wifi_password";
    case RuntimeConfigError::InvalidAgentHost:
      return "invalid_agent_ip";
    case RuntimeConfigError::InvalidAgentPort:
      return "invalid_agent_port";
    case RuntimeConfigError::InvalidApiToken:
      return "invalid_api_token";
    case RuntimeConfigError::UnsupportedVersion:
      return "unsupported_config_version";
  }
  return "unknown_config_error";
}
