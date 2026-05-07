#pragma once

#include <stdint.h>

constexpr uint32_t CODELET_POLL_INTERVAL_MS = 10000;
constexpr uint32_t CODELET_DATA_STALE_MS = 3 * 60 * 1000;
constexpr uint32_t CODELET_FULL_REFRESH_INTERVAL_MS = 30 * 60 * 1000;
constexpr uint8_t CODELET_STRUCTURAL_REFRESHES_BEFORE_FULL = 10;
constexpr uint32_t CODELET_HTTP_TIMEOUT_MS = 8000;
constexpr uint16_t CODELET_SCREEN_WIDTH = 960;
constexpr uint16_t CODELET_SCREEN_HEIGHT = 540;
constexpr uint8_t CODELET_PROJECT_CARD_COUNT = 4;
constexpr const char *CODELET_FIRMWARE_LABEL = "4GRID 0430";
constexpr uint16_t CODELET_HTTP_BUFFER_BYTES = 32768;
constexpr uint16_t CODELET_BUZZ_MS = 120;
constexpr uint16_t CODELET_CONFIG_VERSION = 1;
constexpr uint8_t CODELET_WIFI_SSID_LEN = 64;
constexpr uint8_t CODELET_WIFI_PASSWORD_LEN = 64;
constexpr uint8_t CODELET_AGENT_HOST_LEN = 40;
constexpr uint8_t CODELET_API_TOKEN_LEN = 96;
constexpr uint8_t CODELET_AGENT_BASE_URL_LEN = 96;
constexpr uint32_t CODELET_SETUP_HOLD_MS = 3000;
constexpr uint32_t CODELET_SETUP_CONNECT_TIMEOUT_MS = 15000;
constexpr uint32_t CODELET_SETUP_AGENT_TIMEOUT_MS = 5000;
constexpr const char *CODELET_SETUP_AP_SSID = "Codelet-Setup";
constexpr const char *CODELET_SETUP_AP_PASSWORD = "codelet-setup";
constexpr const char *CODELET_SETUP_AP_URL = "http://192.168.4.1";
