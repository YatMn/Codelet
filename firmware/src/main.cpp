#ifndef CODELET_NATIVE_TEST
#include <Arduino.h>
#include <esp_system.h>
#include <HTTPClient.h>
#include <M5Unified.h>
#include <WiFi.h>

#include "codelet_app.h"
#include "codelet_client.h"
#include "codelet_config_store.h"
#include "codelet_config.h"
#include "codelet_layout.h"
#include "codelet_power_input.h"
#include "codelet_power_intent.h"
#include "codelet_renderer.h"
#include "codelet_runtime_config.h"
#include "codelet_setup_flow.h"
#include "codelet_setup_server.h"

#if __has_include("codelet_secrets.h")
#include "codelet_secrets.h"
#else
#include "codelet_secrets.example.h"
#endif

class ArduinoHttpTransport : public SnapshotTransport {
 public:
  TransportResult get(const char *url, const char *token, char *buffer, size_t bufferSize) override {
    if (buffer == nullptr || bufferSize == 0) {
      return {TransportStatus::NetworkError, 0, "invalid buffer"};
    }

    buffer[0] = '\0';

    HTTPClient http;
    http.begin(url);
    http.setTimeout(CODELET_HTTP_TIMEOUT_MS);
    if (token != nullptr && token[0] != '\0') {
      http.addHeader("X-Codelet-Token", token);
    }
    int code = http.GET();
    if (code <= 0) {
      http.end();
      return {TransportStatus::NetworkError, 0, "network"};
    }
    if (code < 200 || code >= 300) {
      http.end();
      return {TransportStatus::Ok, code, "ok"};
    }

    int payloadSize = http.getSize();
    if (payloadSize >= 0 && static_cast<size_t>(payloadSize) >= bufferSize) {
      http.end();
      return {TransportStatus::NetworkError, code, "payload too large"};
    }

    WiFiClient *stream = http.getStreamPtr();
    size_t bytesWritten = 0;
    uint32_t lastReadMs = millis();
    while (http.connected() && (payloadSize < 0 || bytesWritten < static_cast<size_t>(payloadSize))) {
      size_t available = stream->available();
      if (available == 0) {
        if (millis() - lastReadMs > CODELET_HTTP_TIMEOUT_MS) {
          http.end();
          return {TransportStatus::NetworkError, code, "payload read timeout"};
        }
        delay(1);
        continue;
      }

      size_t remaining = bufferSize - 1 - bytesWritten;
      if (remaining == 0) {
        http.end();
        return {TransportStatus::NetworkError, code, "payload too large"};
      }

      size_t bytesToRead = available < remaining ? available : remaining;
      int bytesRead = stream->readBytes(buffer + bytesWritten, bytesToRead);
      if (bytesRead <= 0) {
        break;
      }

      bytesWritten += static_cast<size_t>(bytesRead);
      lastReadMs = millis();
      buffer[bytesWritten] = '\0';

      if (bytesWritten == bufferSize - 1 && (payloadSize < 0 || bytesWritten < static_cast<size_t>(payloadSize))) {
        http.end();
        return {TransportStatus::NetworkError, code, "payload too large"};
      }
    }

    buffer[bytesWritten] = '\0';
    if (payloadSize >= 0 && bytesWritten < static_cast<size_t>(payloadSize)) {
      http.end();
      return {TransportStatus::NetworkError, code, "payload read failed"};
    }

    http.end();
    return {TransportStatus::Ok, code, "ok"};
  }
};

ArduinoHttpTransport transport;

class DeviceSetupValidator : public SetupConnectionValidator {
 public:
  bool connectWifi(const RuntimeConfig &config, uint32_t timeoutMs) override {
    WiFi.mode(WIFI_AP_STA);
    WiFi.begin(config.wifiSsid, config.wifiPassword);
    uint32_t startMs = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - startMs < timeoutMs) {
      delay(100);
    }
    return WiFi.status() == WL_CONNECTED;
  }

  ClientStatus checkAgent(const RuntimeConfig &config, uint32_t timeoutMs) override {
    char baseUrl[CODELET_AGENT_BASE_URL_LEN];
    if (!buildAgentBaseUrl(config, baseUrl, sizeof(baseUrl))) {
      return ClientStatus::AgentOffline;
    }

    uint32_t startMs = millis();
    CodeletClient validationClient(&transport, baseUrl, config.apiToken);
    CodeletSnapshot snapshot;
    ClientResult result = validationClient.fetchSnapshot(snapshot);
    if (result.status != ClientStatus::Ok && millis() - startMs < timeoutMs) {
      delay(100);
      result = validationClient.fetchSnapshot(snapshot);
    }
    return result.status;
  }
};

PreferencesRuntimeConfigStore configStore;
PreferencesPowerIntentStore powerIntentStore;
RuntimeConfig runtimeConfig = defaultRuntimeConfig();
char runtimeBaseUrl[CODELET_AGENT_BASE_URL_LEN] = "";
CodeletClient client(&transport, runtimeBaseUrl, runtimeConfig.apiToken);
DeviceSetupValidator setupValidator;
CodeletSetupFlow setupFlow(&configStore, &setupValidator);
CodeletSetupServer setupServer(&setupFlow, &configStore);
CodeletApp app;
Layout layout;
CodeletSnapshot pendingSnapshot;
uint32_t lastPollMs = 0;
ShutdownHoldTracker shutdownHoldTracker;

class M5DrawSink : public DrawSink {
 public:
  uint16_t grayColor(uint8_t gray) {
    return static_cast<uint16_t>(((gray & 0xF8) << 8) | ((gray & 0xFC) << 3) | (gray >> 3));
  }

  void clear() override {
    M5.Display.fillScreen(TFT_WHITE);
  }

  void fillRect(Rect rect, uint8_t gray) override {
    if (rect.w <= 0 || rect.h <= 0) {
      return;
    }
    M5.Display.fillRect(rect.x, rect.y, rect.w, rect.h, grayColor(gray));
  }

  void clearRect(Rect rect) override {
    M5.Display.fillRect(rect.x, rect.y, rect.w, rect.h, TFT_WHITE);
  }

  void drawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint8_t gray) override {
    M5.Display.drawLine(x0, y0, x1, y1, grayColor(gray));
  }

  void drawRect(Rect rect, RenderStyle style) override {
    if (rect.w <= 0 || rect.h <= 0) {
      return;
    }
    M5.Display.setClipRect(rect.x, rect.y, rect.w, rect.h);
    int16_t right = rect.x + rect.w - 1;
    int16_t bottom = rect.y + rect.h - 1;
    M5.Display.drawRect(rect.x, rect.y, rect.w, rect.h, TFT_BLACK);
    if (style.strongBorder) {
      for (int offset = 1; offset < 4; offset++) {
        if (rect.w > offset * 2 && rect.h > offset * 2) {
          M5.Display.drawRect(rect.x + offset, rect.y + offset, rect.w - offset * 2, rect.h - offset * 2,
                              TFT_BLACK);
        }
      }
    }
    if (style.hatched) {
      for (int16_t start = rect.x - rect.h; start <= right; start += 12) {
        int16_t x0 = start < rect.x ? rect.x : start;
        int16_t y0 = static_cast<int16_t>(rect.y + (x0 - start));
        int16_t x1 = start + rect.h;
        if (x1 > right) {
          x1 = right;
        }
        int16_t y1 = static_cast<int16_t>(rect.y + (x1 - start));
        if (y1 > bottom) {
          x1 = static_cast<int16_t>(x1 - (y1 - bottom));
          y1 = bottom;
        }
        if (y0 <= bottom && y1 >= rect.y) {
          M5.Display.drawLine(x0, y0, x1, y1, grayColor(164));
        }
      }
    }
    if (style.dashedBorder) {
      for (int16_t x = rect.x; x <= right; x += 18) {
        int16_t endX = x + 10;
        if (endX > right) {
          endX = right;
        }
        M5.Display.drawLine(x, rect.y, endX, rect.y, TFT_BLACK);
      }
    }
    M5.Display.clearClipRect();
  }

  void drawText(Rect rect, const char *text, TextRole role, bool inverted = false) override {
    if (inverted) {
      M5.Display.fillRect(rect.x, rect.y, rect.w, rect.h, TFT_BLACK);
      M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
    } else {
      M5.Display.setTextColor(TFT_BLACK, TFT_WHITE);
    }
    switch (role) {
      case TextRole::Brand:
      case TextRole::ProjectTitle:
        M5.Display.setFont(&fonts::efontCN_24_b);
        break;
      case TextRole::PrimaryText:
      case TextRole::BadgeText:
        M5.Display.setFont(&fonts::efontCN_16_b);
        break;
      case TextRole::MetaText:
        M5.Display.setFont(&fonts::efontCN_16);
        break;
    }
    M5.Display.setTextSize(role == TextRole::Brand ? 2 : 1);
    M5.Display.setTextWrap(false);
    M5.Display.setClipRect(rect.x, rect.y, rect.w, rect.h);
    int16_t yOffset = role == TextRole::Brand ? 1 : ((role == TextRole::ProjectTitle) ? 3 : 2);
    M5.Display.setCursor(rect.x + 6, rect.y + yOffset);
    M5.Display.print(text == nullptr ? "" : text);
    M5.Display.clearClipRect();
  }

  void commit(Rect rect) override {
    M5.Display.waitDisplay();
    M5.Display.display(rect.x, rect.y, rect.w, rect.h);
  }
};

M5DrawSink sink;

void setDisplayModeForRender(RenderMode mode) {
  M5.Display.setEpdMode(mode == RenderMode::Full ? epd_mode_t::epd_quality : epd_mode_t::epd_text);
}

DevicePowerStatus currentDevicePowerStatus();
void clearShutdownIntentMarker();

void renderCurrentPage(RenderMode mode) {
  setDisplayModeForRender(mode);
  DevicePowerStatus power = currentDevicePowerStatus();
  switch (app.page()) {
    case AppPage::Home:
      renderHome(sink, layout, app.snapshot(), mode, power);
      break;
    case AppPage::Threads:
      renderThreads(sink, layout, app.snapshot(), mode, power);
      break;
    case AppPage::ProjectDetail:
      renderProjectDetail(sink, layout, app.snapshot(), app.selectedProjectId(), mode, power);
      break;
    case AppPage::Offline:
      renderStatusPage(sink, layout, "AGENT OFFLINE", "Desktop Agent unreachable", runtimeBaseUrl);
      break;
    case AppPage::AuthFailed:
      renderStatusPage(sink, layout, "AUTH FAILED", "Check API token in Setup Mode", runtimeBaseUrl);
      break;
    case AppPage::DataStale:
      renderStatusPage(sink, layout, "DATA STALE", "Last good snapshot is too old", runtimeBaseUrl);
      break;
  }
}

DevicePowerStatus currentDevicePowerStatus() {
  int battery = M5.Power.getBatteryLevel();
  bool known = battery >= 0 && battery <= 100;
  bool charging = static_cast<int>(M5.Power.isCharging()) == 1;
  return {battery, charging, known};
}

void renderSetupScreen(const char *status) {
  setDisplayModeForRender(RenderMode::Full);
  sink.clear();
  sink.drawText({80, 72, 800, 48}, "SETUP MODE", TextRole::Brand);
  sink.drawText({80, 144, 800, 34}, status == nullptr ? "Configure Codelet connection" : status,
                TextRole::PrimaryText);
  sink.drawText({80, 210, 800, 34}, "Wi-Fi: Codelet-Setup", TextRole::PrimaryText);
  sink.drawText({80, 254, 800, 34}, "Password: codelet-setup", TextRole::PrimaryText);
  sink.drawText({80, 298, 800, 34}, "Open: http://192.168.4.1", TextRole::PrimaryText);
  sink.drawText({80, 374, 800, 34}, "Save Wi-Fi and Desktop Agent IP to continue.", TextRole::MetaText);
  sink.commit({0, 0, CODELET_SCREEN_WIDTH, CODELET_SCREEN_HEIGHT});
}

void renderSetupFailureScreen(const char *status) {
  setDisplayModeForRender(RenderMode::Full);
  sink.clear();
  sink.drawText({80, 72, 800, 48}, "SETUP MODE", TextRole::Brand);
  sink.drawText({80, 144, 800, 34}, status == nullptr ? "Setup AP failed." : status, TextRole::PrimaryText);
  sink.drawText({80, 210, 800, 34}, "Portal is not running.", TextRole::PrimaryText);
  sink.drawText({80, 254, 800, 34}, "Restart Codelet and try setup again.", TextRole::MetaText);
  sink.commit({0, 0, CODELET_SCREEN_WIDTH, CODELET_SCREEN_HEIGHT});
}

void renderShutdownAndPowerOff() {
  renderStatusPage(sink, layout, "SHUTTING DOWN", "Powering off now", "Press side power to start again");
  M5.Display.waitDisplay();
  delay(250);
  M5.Power.powerOff();
}

void requestShutdown(const char *reason) {
  Serial.println(reason == nullptr ? "shutdown_requested" : reason);
  clearShutdownIntentMarker();
  Serial.flush();
  renderShutdownAndPowerOff();
}

bool isTopLeftTouchHeld() {
  uint32_t holdStartMs = 0;
  uint32_t startMs = millis();
  while (millis() - startMs <= CODELET_SETUP_HOLD_MS + 250) {
    M5.update();
    auto touch = M5.Touch.getDetail();
    bool inSetupCorner = touch.isPressed() && touch.x >= 0 && touch.y >= 0 && touch.x < 160 && touch.y < 160;
    if (inSetupCorner) {
      if (holdStartMs == 0) {
        holdStartMs = millis();
      }
      if (millis() - holdStartMs >= CODELET_SETUP_HOLD_MS) {
        return true;
      }
    } else {
      holdStartMs = 0;
    }
    delay(20);
  }
  return false;
}

bool loadRuntimeConfig() {
  RuntimeConfig loaded = defaultRuntimeConfig();
  if (!configStore.load(loaded) || !validateRuntimeConfig(loaded).valid) {
    runtimeConfig = defaultRuntimeConfig();
    runtimeBaseUrl[0] = '\0';
    return false;
  }
  if (!buildAgentBaseUrl(loaded, runtimeBaseUrl, sizeof(runtimeBaseUrl))) {
    runtimeConfig = defaultRuntimeConfig();
    runtimeBaseUrl[0] = '\0';
    return false;
  }
  runtimeConfig = loaded;
  return true;
}

bool startSetupPortal(const char *status) {
  renderSetupScreen(status);
  if (!setupServer.isRunning()) {
    if (!setupServer.begin()) {
      renderSetupFailureScreen("Setup AP failed.");
      return false;
    }
  }
  return true;
}

void connectWifi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(runtimeConfig.wifiSsid, runtimeConfig.wifiPassword);
}

CodeletResetReason currentResetReason() {
  switch (esp_reset_reason()) {
    case ESP_RST_POWERON: return CodeletResetReason::PowerOn;
    case ESP_RST_EXT: return CodeletResetReason::External;
    case ESP_RST_SW: return CodeletResetReason::Software;
    case ESP_RST_PANIC: return CodeletResetReason::Panic;
    case ESP_RST_INT_WDT:
    case ESP_RST_TASK_WDT:
    case ESP_RST_WDT: return CodeletResetReason::Watchdog;
    case ESP_RST_DEEPSLEEP: return CodeletResetReason::DeepSleep;
    case ESP_RST_BROWNOUT: return CodeletResetReason::Brownout;
#if defined(ESP_RST_USB)
    case ESP_RST_USB: return CodeletResetReason::Usb;
#endif
#if defined(ESP_RST_JTAG)
    case ESP_RST_JTAG: return CodeletResetReason::Jtag;
#endif
#if defined(ESP_RST_PWR_GLITCH)
    case ESP_RST_PWR_GLITCH: return CodeletResetReason::PowerGlitch;
#endif
    default: return CodeletResetReason::Unknown;
  }
}

void clearShutdownIntentMarker() {
  if (!powerIntentStore.clearShutdownArmed()) {
    Serial.println("shutdown_intent_clear_failed");
  }
}

void armShutdownIntentMarker() {
  if (!powerIntentStore.saveShutdownArmed(true)) {
    Serial.println("shutdown_intent_arm_failed");
  }
}

bool maybeShutdownFromBootPowerIntent() {
  bool shutdownArmed = false;
  bool markerLoaded = powerIntentStore.loadShutdownArmed(shutdownArmed);
  CodeletResetReason resetReason = currentResetReason();
  Serial.printf("reset_reason=%s shutdown_armed=%d marker_loaded=%d\n",
                resetReasonName(resetReason), shutdownArmed ? 1 : 0, markerLoaded ? 1 : 0);

  BootPowerIntentDecision decision = decideBootPowerIntent(shutdownArmed, resetReason);
  if (!decision.shutdownRequested) {
    return false;
  }

  Serial.println(decision.reason);
  clearShutdownIntentMarker();
  renderStatusPage(sink, layout, "SHUTTING DOWN", "Physical key requested power off", "Press side power to start");
  M5.Display.waitDisplay();
  Serial.flush();
  delay(150);
  M5.Power.powerOff();
  return true;
}

void setup() {
  auto cfg = M5.config();
  M5.begin(cfg);
  Serial.begin(115200);
  layout = buildLayout();
  Serial.printf("codelet_fw=%s home_cards=%d\n", CODELET_FIRMWARE_LABEL, layout.homeCardCount);
  M5.Display.setRotation(1);
  M5.Display.setTextDatum(top_left);
  M5.Display.setTextWrap(false);
  M5.Display.setEpdMode(epd_mode_t::epd_quality);

  if (maybeShutdownFromBootPowerIntent()) {
    return;
  }

  bool manualSetup = isTopLeftTouchHeld();
  SetupStartDecision setupDecision = setupFlow.begin(manualSetup);
  if (setupDecision.enterSetup) {
    startSetupPortal(manualSetup ? "Manual setup requested" : "Runtime config required");
    return;
  }

  if (!loadRuntimeConfig()) {
    setupFlow.begin(false);
    startSetupPortal("Runtime config invalid");
    return;
  }

  renderStatusPage(sink, layout, "STARTING", "Wi-Fi connecting", runtimeBaseUrl);
  armShutdownIntentMarker();
  connectWifi();
}

void loop() {
  M5.update();
  if (M5.BtnPWR.wasClicked()) {
    requestShutdown("power_button_shutdown");
    return;
  }

  if (setupServer.isRunning()) {
    setupServer.handleClient();
    if (setupFlow.state() == SetupState::Succeeded) {
      setupServer.stop();
      if (!loadRuntimeConfig()) {
        setupFlow.begin(false);
        startSetupPortal("Runtime config reload failed");
        return;
      }
      renderStatusPage(sink, layout, "STARTING", "Wi-Fi connecting", runtimeBaseUrl);
      armShutdownIntentMarker();
      connectWifi();
      renderCurrentPage(RenderMode::Full);
      app.markRefreshed(millis(), RefreshMode::Full);
      lastPollMs = 0;
    }
    delay(20);
    return;
  }

  uint32_t now = millis();
  auto touch = M5.Touch.getDetail();
  PowerTouchSample shutdownTouch{
      static_cast<int16_t>(touch.x),
      static_cast<int16_t>(touch.y),
      touch.isPressed(),
  };
  if (shutdownHoldTracker.update(shutdownTouch, now)) {
    requestShutdown("touch_hold_shutdown");
    return;
  }
  if (touch.wasClicked() || touch.wasFlicked()) {
    TouchEvent event{};
    event.x = static_cast<int16_t>(touch.x);
    event.y = static_cast<int16_t>(touch.y);
    event.pressed = touch.wasClicked();
    event.flicked = touch.wasFlicked();
    event.distanceX = static_cast<int16_t>(touch.distanceX());
    event.distanceY = static_cast<int16_t>(touch.distanceY());
    bool pageChanged = app.handleTouch(event, layout);
    if (pageChanged) {
      RefreshMode refreshMode = app.shouldFullRefresh(now) ? RefreshMode::Full : app.pendingRefreshMode();
      RenderMode mode = refreshMode == RefreshMode::Full
                            ? RenderMode::Full
                            : refreshMode == RefreshMode::Structural ? RenderMode::Structural : RenderMode::Partial;
      renderCurrentPage(mode);
      app.markRefreshed(now, refreshMode);
    }
  }

  if (now - lastPollMs >= CODELET_POLL_INTERVAL_MS) {
    lastPollMs = now;
    if (WiFi.status() != WL_CONNECTED) {
      connectWifi();
      Serial.println("wifi_disconnected");
      if (app.shouldShowConnectionFailure(now) && app.setOfflinePage(app.connectionFailurePage(now))) {
        renderCurrentPage(RenderMode::Full);
        app.markRefreshed(now);
      }
      return;
    }
    ClientResult result = client.fetchSnapshot(pendingSnapshot);
    Serial.printf("%s %s\n", CODELET_FIRMWARE_LABEL, result.message);
    if (result.status == ClientStatus::Ok) {
      ApplyResult apply = app.applySnapshot(pendingSnapshot, now);
      if (app.shouldBuzz(pendingSnapshot, now)) {
        M5.Speaker.tone(2400, CODELET_BUZZ_MS);
      }
      bool fullRefresh = app.shouldFullRefresh(now);
      if (apply.needsRefresh || fullRefresh) {
        RefreshMode refreshMode = fullRefresh ? RefreshMode::Full : apply.refreshMode;
        RenderMode mode = refreshMode == RefreshMode::Full
                              ? RenderMode::Full
                              : refreshMode == RefreshMode::Structural ? RenderMode::Structural : RenderMode::Partial;
        renderCurrentPage(mode);
        app.markRefreshed(now, refreshMode);
      }
    } else {
      bool pageChanged = false;
      if (result.status == ClientStatus::AgentOffline) {
        if (app.shouldShowConnectionFailure(now)) {
          pageChanged = app.setOfflinePage(app.connectionFailurePage(now));
        }
      } else if (result.status == ClientStatus::AuthFailed) {
        pageChanged = app.setOfflinePage(AppPage::AuthFailed);
      } else if (result.status == ClientStatus::InvalidData) {
        if (app.shouldShowConnectionFailure(now)) {
          pageChanged = app.setOfflinePage(app.connectionFailurePage(now));
        }
      }
      if (pageChanged) {
        renderCurrentPage(RenderMode::Full);
        app.markRefreshed(now);
      }
    }
  }
  delay(20);
}
#endif
