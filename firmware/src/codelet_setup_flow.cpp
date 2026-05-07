#include "codelet_setup_flow.h"

CodeletSetupFlow::CodeletSetupFlow(RuntimeConfigStore *store, SetupConnectionValidator *validator)
    : store_(store), validator_(validator) {}

SetupStartDecision CodeletSetupFlow::begin(bool setupGestureHeld) {
  lastError_ = SetupError::None;
  lastConfigError_ = RuntimeConfigError::None;

  if (setupGestureHeld) {
    state_ = SetupState::ShowingForm;
    reason_ = SetupModeReason::ManualGesture;
    return {true, reason_};
  }

  bool hasConfig = store_ != nullptr && hasValidRuntimeConfig(*store_);
  if (!hasConfig) {
    state_ = SetupState::ShowingForm;
    reason_ = SetupModeReason::MissingConfig;
    return {true, reason_};
  }

  state_ = SetupState::Inactive;
  reason_ = SetupModeReason::None;
  return {false, reason_};
}

SetupSubmitResult CodeletSetupFlow::submit(const RuntimeConfig &config) {
  if (state_ != SetupState::ShowingForm && state_ != SetupState::ShowingError) {
    lastError_ = SetupError::InvalidState;
    lastConfigError_ = RuntimeConfigError::None;
    return {false, lastError_, lastConfigError_};
  }

  RuntimeConfigValidation validation = validateRuntimeConfig(config);
  if (!validation.valid) {
    state_ = SetupState::ShowingError;
    lastError_ = SetupError::InvalidConfig;
    lastConfigError_ = validation.error;
    return {false, lastError_, lastConfigError_};
  }

  if (store_ == nullptr || !store_->save(config)) {
    state_ = SetupState::ShowingError;
    lastError_ = SetupError::SaveFailed;
    lastConfigError_ = RuntimeConfigError::None;
    return {false, lastError_, lastConfigError_};
  }

  state_ = SetupState::TestingConnection;
  if (validator_ == nullptr || !validator_->connectWifi(config, CODELET_SETUP_CONNECT_TIMEOUT_MS)) {
    state_ = SetupState::ShowingError;
    lastError_ = SetupError::WifiFailed;
    lastConfigError_ = RuntimeConfigError::None;
    return {false, lastError_, lastConfigError_};
  }

  ClientStatus agentStatus = validator_->checkAgent(config, CODELET_SETUP_AGENT_TIMEOUT_MS);
  if (agentStatus == ClientStatus::AuthFailed) {
    state_ = SetupState::ShowingError;
    lastError_ = SetupError::AuthFailed;
    lastConfigError_ = RuntimeConfigError::None;
    return {false, lastError_, lastConfigError_};
  }
  if (agentStatus != ClientStatus::Ok) {
    state_ = SetupState::ShowingError;
    lastError_ = SetupError::AgentUnreachable;
    lastConfigError_ = RuntimeConfigError::None;
    return {false, lastError_, lastConfigError_};
  }

  state_ = SetupState::Succeeded;
  lastError_ = SetupError::None;
  lastConfigError_ = RuntimeConfigError::None;
  return {true, SetupError::None, RuntimeConfigError::None};
}

SetupState CodeletSetupFlow::state() const {
  return state_;
}

SetupModeReason CodeletSetupFlow::reason() const {
  return reason_;
}

SetupError CodeletSetupFlow::lastError() const {
  return lastError_;
}

RuntimeConfigError CodeletSetupFlow::lastConfigError() const {
  return lastConfigError_;
}

const char *setupErrorLabel(SetupError error) {
  switch (error) {
    case SetupError::None:
      return "ok";
    case SetupError::InvalidState:
      return "invalid_state";
    case SetupError::InvalidConfig:
      return "invalid_config";
    case SetupError::SaveFailed:
      return "save_failed";
    case SetupError::WifiFailed:
      return "wifi_connection_failed";
    case SetupError::AgentUnreachable:
      return "agent_unreachable";
    case SetupError::AuthFailed:
      return "auth_failed";
  }
  return "unknown_setup_error";
}
