#include "codelet_status.h"

#include <string.h>

CodeletStatus parseStatus(const char *value) {
  if (value == nullptr) {
    return CodeletStatus::Unknown;
  }
  if (strcmp(value, "approval_required") == 0) return CodeletStatus::ApprovalRequired;
  if (strcmp(value, "failed") == 0) return CodeletStatus::Failed;
  if (strcmp(value, "awaiting_user") == 0) return CodeletStatus::AwaitingUser;
  if (strcmp(value, "stale") == 0) return CodeletStatus::Stale;
  if (strcmp(value, "long_running") == 0) return CodeletStatus::LongRunning;
  if (strcmp(value, "tool_running") == 0) return CodeletStatus::ToolRunning;
  if (strcmp(value, "running") == 0) return CodeletStatus::Running;
  if (strcmp(value, "thinking") == 0) return CodeletStatus::Thinking;
  if (strcmp(value, "completed") == 0) return CodeletStatus::Completed;
  if (strcmp(value, "idle") == 0) return CodeletStatus::Idle;
  if (strcmp(value, "cancelled") == 0) return CodeletStatus::Cancelled;
  return CodeletStatus::Unknown;
}

int priorityForStatus(CodeletStatus status) {
  switch (status) {
    case CodeletStatus::ApprovalRequired: return 110;
    case CodeletStatus::Failed: return 100;
    case CodeletStatus::AwaitingUser: return 90;
    case CodeletStatus::Stale: return 80;
    case CodeletStatus::LongRunning: return 70;
    case CodeletStatus::ToolRunning: return 60;
    case CodeletStatus::Running: return 50;
    case CodeletStatus::Thinking: return 40;
    case CodeletStatus::Completed: return 30;
    case CodeletStatus::Idle: return 20;
    case CodeletStatus::Cancelled: return 10;
    case CodeletStatus::Unknown: return 0;
  }
  return 0;
}

const char *statusLabel(CodeletStatus status) {
  switch (status) {
    case CodeletStatus::ApprovalRequired: return "approval";
    case CodeletStatus::Failed: return "failed";
    case CodeletStatus::AwaitingUser: return "awaiting";
    case CodeletStatus::Stale: return "stale";
    case CodeletStatus::LongRunning: return "long";
    case CodeletStatus::ToolRunning: return "tool";
    case CodeletStatus::Running: return "running";
    case CodeletStatus::Thinking: return "thinking";
    case CodeletStatus::Completed: return "done";
    case CodeletStatus::Idle: return "idle";
    case CodeletStatus::Cancelled: return "cancelled";
    case CodeletStatus::Unknown: return "unknown";
  }
  return "unknown";
}

bool usesHatchedBackground(CodeletStatus status) {
  (void)status;
  return false;
}

bool usesDashedBorder(CodeletStatus status) {
  return status == CodeletStatus::Stale;
}

bool usesStrongBorder(CodeletStatus status) {
  return status == CodeletStatus::Failed;
}
