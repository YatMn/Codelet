#pragma once

#include <stddef.h>
#include <stdint.h>

#include "codelet_config.h"

struct RuntimeConfig {
  char wifiSsid[CODELET_WIFI_SSID_LEN];
  char wifiPassword[CODELET_WIFI_PASSWORD_LEN];
  char agentHost[CODELET_AGENT_HOST_LEN];
  uint16_t agentPort;
  char apiToken[CODELET_API_TOKEN_LEN];
  uint16_t configVersion;
};

enum class RuntimeConfigError : uint8_t {
  None,
  MissingWifiSsid,
  MissingWifiPassword,
  InvalidAgentHost,
  InvalidAgentPort,
  InvalidApiToken,
  UnsupportedVersion,
};

struct RuntimeConfigValidation {
  bool valid;
  RuntimeConfigError error;
};

RuntimeConfig defaultRuntimeConfig();
RuntimeConfigValidation validateRuntimeConfig(const RuntimeConfig &config);
bool isValidIpv4Address(const char *value);
bool buildAgentBaseUrl(const RuntimeConfig &config, char *buffer, size_t bufferSize);
const char *runtimeConfigErrorLabel(RuntimeConfigError error);
