#pragma once

#include <stdint.h>

#include "codelet_client.h"
#include "codelet_config_store.h"

enum class SetupState : uint8_t {
  Inactive,
  ShowingForm,
  TestingConnection,
  ShowingError,
  Succeeded,
};

enum class SetupModeReason : uint8_t {
  None,
  MissingConfig,
  ManualGesture,
};

enum class SetupError : uint8_t {
  None,
  InvalidState,
  InvalidConfig,
  SaveFailed,
  WifiFailed,
  AgentUnreachable,
  AuthFailed,
};

struct SetupStartDecision {
  bool enterSetup;
  SetupModeReason reason;
};

struct SetupSubmitResult {
  bool success;
  SetupError error;
  RuntimeConfigError configError;
};

class SetupConnectionValidator {
 public:
  virtual ~SetupConnectionValidator() = default;
  virtual bool connectWifi(const RuntimeConfig &config, uint32_t timeoutMs) = 0;
  virtual ClientStatus checkAgent(const RuntimeConfig &config, uint32_t timeoutMs) = 0;
};

class CodeletSetupFlow {
 public:
  CodeletSetupFlow(RuntimeConfigStore *store, SetupConnectionValidator *validator);

  SetupStartDecision begin(bool setupGestureHeld);
  SetupSubmitResult submit(const RuntimeConfig &config);
  SetupState state() const;
  SetupModeReason reason() const;
  SetupError lastError() const;
  RuntimeConfigError lastConfigError() const;

 private:
  RuntimeConfigStore *store_;
  SetupConnectionValidator *validator_;
  SetupState state_ = SetupState::Inactive;
  SetupModeReason reason_ = SetupModeReason::None;
  SetupError lastError_ = SetupError::None;
  RuntimeConfigError lastConfigError_ = RuntimeConfigError::None;
};

const char *setupErrorLabel(SetupError error);
