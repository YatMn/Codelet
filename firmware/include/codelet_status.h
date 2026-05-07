#pragma once

#include <stdint.h>

enum class CodeletStatus : uint8_t {
  ApprovalRequired,
  Failed,
  AwaitingUser,
  Stale,
  LongRunning,
  ToolRunning,
  Running,
  Thinking,
  Completed,
  Idle,
  Cancelled,
  Unknown,
};

CodeletStatus parseStatus(const char *value);
int priorityForStatus(CodeletStatus status);
const char *statusLabel(CodeletStatus status);
bool usesHatchedBackground(CodeletStatus status);
bool usesDashedBorder(CodeletStatus status);
bool usesStrongBorder(CodeletStatus status);
