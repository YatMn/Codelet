#include "codelet_setup_server.h"

#ifndef CODELET_NATIVE_TEST
#include <stdio.h>
#include <string.h>

#include <WiFi.h>

namespace {

IPAddress kApIp(192, 168, 4, 1);
IPAddress kGateway(192, 168, 4, 1);
IPAddress kSubnet(255, 255, 255, 0);
constexpr uint16_t kDnsPort = 53;

const char *htmlHeader() {
  return "<!doctype html><html><head><meta name='viewport' content='width=device-width,initial-scale=1'>"
         "<title>Codelet Setup</title><style>"
         "body{font-family:-apple-system,BlinkMacSystemFont,Segoe UI,sans-serif;margin:24px;max-width:520px}"
         "label{display:block;margin-top:14px;font-weight:600}"
         "input{width:100%;box-sizing:border-box;font-size:16px;padding:10px;margin-top:6px}"
         "button{margin-top:20px;font-size:16px;padding:10px 14px}"
         ".status{margin-top:18px;padding:12px;border:1px solid #999}"
         "</style></head><body><h1>Codelet Setup</h1>";
}

const char *htmlFooter() {
  return "</body></html>";
}

void copyArg(WebServer &server, const char *name, char *buffer, size_t bufferSize) {
  if (buffer == nullptr || bufferSize == 0) {
    return;
  }
  String value = server.arg(name);
  strncpy(buffer, value.c_str(), bufferSize - 1);
  buffer[bufferSize - 1] = '\0';
}

void copyStatus(char *buffer, size_t bufferSize, const char *value) {
  if (buffer == nullptr || bufferSize == 0) {
    return;
  }
  strncpy(buffer, value, bufferSize - 1);
  buffer[bufferSize - 1] = '\0';
}

bool isArgTooLong(WebServer &server, const char *name, size_t bufferSize) {
  return server.arg(name).length() >= bufferSize;
}

bool validateRequestLengths(WebServer &server, char *status, size_t statusSize) {
  if (isArgTooLong(server, "wifi_ssid", CODELET_WIFI_SSID_LEN)) {
    copyStatus(status, statusSize, "Invalid config: wifi_ssid_too_long");
    return false;
  }
  if (isArgTooLong(server, "wifi_password", CODELET_WIFI_PASSWORD_LEN)) {
    copyStatus(status, statusSize, "Invalid config: wifi_password_too_long");
    return false;
  }
  if (isArgTooLong(server, "agent_host", CODELET_AGENT_HOST_LEN)) {
    copyStatus(status, statusSize, "Invalid config: agent_host_too_long");
    return false;
  }
  if (isArgTooLong(server, "api_token", CODELET_API_TOKEN_LEN)) {
    copyStatus(status, statusSize, "Invalid config: api_token_too_long");
    return false;
  }
  return true;
}

String htmlAttributeEscape(const char *value) {
  String escaped;
  if (value == nullptr) {
    return escaped;
  }

  for (const char *cursor = value; *cursor != '\0'; cursor++) {
    switch (*cursor) {
      case '&':
        escaped += "&amp;";
        break;
      case '<':
        escaped += "&lt;";
        break;
      case '>':
        escaped += "&gt;";
        break;
      case '"':
        escaped += "&quot;";
        break;
      case '\'':
        escaped += "&#39;";
        break;
      default:
        escaped += *cursor;
        break;
    }
  }
  return escaped;
}

bool parseStrictPort(const String &value, uint16_t &port) {
  if (value.length() == 0) {
    return false;
  }

  uint32_t parsed = 0;
  for (size_t index = 0; index < value.length(); index++) {
    char digit = value.charAt(index);
    if (digit < '0' || digit > '9') {
      return false;
    }
    parsed = parsed * 10 + static_cast<uint32_t>(digit - '0');
    if (parsed > 65535) {
      return false;
    }
  }

  if (parsed == 0) {
    return false;
  }
  port = static_cast<uint16_t>(parsed);
  return true;
}

}  // namespace

CodeletSetupServer::CodeletSetupServer(CodeletSetupFlow *flow, RuntimeConfigStore *store)
    : flow_(flow), store_(store), server_(80) {}

bool CodeletSetupServer::begin() {
  WiFi.mode(WIFI_AP_STA);
  if (!WiFi.softAPConfig(kApIp, kGateway, kSubnet)) {
    WiFi.softAPdisconnect(true);
    return false;
  }
  if (!WiFi.softAP(CODELET_SETUP_AP_SSID, CODELET_SETUP_AP_PASSWORD)) {
    WiFi.softAPdisconnect(true);
    return false;
  }
  if (!dns_.start(kDnsPort, "*", kApIp)) {
    dns_.stop();
    WiFi.softAPdisconnect(true);
    return false;
  }

  server_.on("/", HTTP_GET, [this]() { handleRoot(); });
  server_.on("/save", HTTP_POST, [this]() { handleSave(); });
  server_.onNotFound([this]() { handleRoot(); });
  server_.begin();
  running_ = true;
  copyStatus(lastStatus_, sizeof(lastStatus_), "Ready");
  return true;
}

void CodeletSetupServer::handleClient() {
  if (!running_) {
    return;
  }
  dns_.processNextRequest();
  server_.handleClient();
}

void CodeletSetupServer::stop() {
  if (!running_) {
    return;
  }
  server_.stop();
  dns_.stop();
  WiFi.softAPdisconnect(true);
  running_ = false;
}

bool CodeletSetupServer::isRunning() const {
  return running_;
}

void CodeletSetupServer::handleRoot() {
  sendForm(lastStatus_);
}

void CodeletSetupServer::handleSave() {
  if (flow_ == nullptr) {
    copyStatus(lastStatus_, sizeof(lastStatus_), "Setup unavailable");
    sendForm(lastStatus_);
    return;
  }

  if (!validateRequestLengths(server_, lastStatus_, sizeof(lastStatus_))) {
    sendForm(lastStatus_);
    return;
  }

  RuntimeConfig config = defaultRuntimeConfig();
  if (!configFromRequest(config, lastStatus_, sizeof(lastStatus_))) {
    sendForm(lastStatus_);
    return;
  }

  copyStatus(lastStatus_, sizeof(lastStatus_), "Connecting");

  SetupSubmitResult result = flow_->submit(config);
  if (result.success) {
    copyStatus(lastStatus_, sizeof(lastStatus_), "Success. Device will enter Codelet.");
  } else if (result.error == SetupError::InvalidConfig) {
    snprintf(lastStatus_, sizeof(lastStatus_), "Invalid config: %s", runtimeConfigErrorLabel(result.configError));
  } else {
    snprintf(lastStatus_, sizeof(lastStatus_), "Connection failed: %s", setupErrorLabel(result.error));
  }
  lastStatus_[sizeof(lastStatus_) - 1] = '\0';
  sendForm(lastStatus_);
}

void CodeletSetupServer::sendForm(const char *statusMessage) {
  RuntimeConfig savedConfig = defaultRuntimeConfig();
  bool hasSavedConfig = loadSavedConfig(savedConfig);

  String html = htmlHeader();
  html += "<form method='post' action='/save'>";
  html += "<label>Wi-Fi SSID<input name='wifi_ssid' value='";
  html += hasSavedConfig ? htmlAttributeEscape(savedConfig.wifiSsid) : "";
  html += "' required></label>";
  html += "<label>Wi-Fi password<input name='wifi_password' type='password'";
  html += hasSavedConfig ? "" : " required";
  html += "></label>";
  html += "<label>Agent IP<input name='agent_host' inputmode='numeric' placeholder='192.168.1.23' value='";
  html += hasSavedConfig ? htmlAttributeEscape(savedConfig.agentHost) : "";
  html += "' required></label>";
  html += "<label>Agent port<input name='agent_port' inputmode='numeric' value='";
  html += hasSavedConfig ? String(savedConfig.agentPort) : "8765";
  html += "' required></label>";
  html += "<label>API token optional<input name='api_token' type='password'></label>";
  html += "<button type='submit'>Save and Test Connection</button></form>";
  html += "<div class='status'>";
  html += statusMessage == nullptr ? "" : statusMessage;
  html += "</div>";
  html += htmlFooter();
  server_.send(200, "text/html", html);
}

bool CodeletSetupServer::loadSavedConfig(RuntimeConfig &config) {
  if (store_ == nullptr || !store_->load(config)) {
    return false;
  }
  return validateRuntimeConfig(config).valid;
}

bool CodeletSetupServer::configFromRequest(RuntimeConfig &config, char *status, size_t statusSize) {
  RuntimeConfig savedConfig = defaultRuntimeConfig();
  bool hasSavedConfig = loadSavedConfig(savedConfig);
  config = hasSavedConfig ? savedConfig : defaultRuntimeConfig();

  copyArg(server_, "wifi_ssid", config.wifiSsid, sizeof(config.wifiSsid));
  copyArg(server_, "agent_host", config.agentHost, sizeof(config.agentHost));

  if (server_.arg("wifi_password").length() > 0 || !hasSavedConfig) {
    copyArg(server_, "wifi_password", config.wifiPassword, sizeof(config.wifiPassword));
  }
  if (server_.arg("api_token").length() > 0 || !hasSavedConfig) {
    copyArg(server_, "api_token", config.apiToken, sizeof(config.apiToken));
  }

  uint16_t port = 0;
  if (!parseStrictPort(server_.arg("agent_port"), port)) {
    copyStatus(status, statusSize, "Invalid config: invalid_agent_port");
    return false;
  }
  config.agentPort = port;
  config.configVersion = CODELET_CONFIG_VERSION;
  return true;
}
#endif
