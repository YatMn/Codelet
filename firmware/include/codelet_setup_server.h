#pragma once

#include "codelet_setup_flow.h"

#ifndef CODELET_NATIVE_TEST
#include <DNSServer.h>
#include <WebServer.h>

class CodeletSetupServer {
 public:
  CodeletSetupServer(CodeletSetupFlow *flow, RuntimeConfigStore *store);

  bool begin();
  void handleClient();
  void stop();
  bool isRunning() const;

 private:
  void handleRoot();
  void handleSave();
  void sendForm(const char *statusMessage);
  bool loadSavedConfig(RuntimeConfig &config);
  bool configFromRequest(RuntimeConfig &config, char *status, size_t statusSize);

  CodeletSetupFlow *flow_;
  RuntimeConfigStore *store_;
  WebServer server_;
  DNSServer dns_;
  bool running_ = false;
  char lastStatus_[96] = "";
};
#endif
