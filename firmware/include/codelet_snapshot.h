#pragma once

#include <stddef.h>
#include <stdint.h>

#include "codelet_status.h"

constexpr size_t CODELET_MAX_PROJECTS = 6;
constexpr size_t CODELET_MAX_THREADS = 24;
constexpr size_t CODELET_MAX_RECENT_THREADS_PER_PROJECT = 2;
constexpr size_t CODELET_MAX_QUOTA_BUCKETS = 2;
constexpr size_t CODELET_MAX_SNAPSHOT_JSON_BYTES = 32768;
constexpr size_t CODELET_ID_LEN = 96;
constexpr size_t CODELET_TEXT_LEN = 64;
constexpr size_t CODELET_LAST_EVENT_LEN = 96;

enum class ParseStatus : uint8_t {
  Ok,
  InvalidJson,
  MissingServer,
};

struct ParseResult {
  ParseStatus status;
  const char *message;
};

struct QuotaBucketView {
  char kind[CODELET_TEXT_LEN];
  char label[16];
  int usedPercent;
  int remainingPercent;
  int resetInSec;
  char status[24];
};

struct ProjectCountsView {
  int approvalRequired;
  int failed;
  int awaitingUser;
  int running;
  int toolRunning;
  int longRunning;
  int stale;
  int completed;
};

struct ProjectThreadSummaryView {
  char id[CODELET_ID_LEN];
  char title[CODELET_TEXT_LEN];
  CodeletStatus status;
  int attentionPriority;
  int durationSec;
  char lastEvent[CODELET_LAST_EVENT_LEN];
  bool approvalRequired;
  int displayVersion;
};

struct ProjectView {
  char id[CODELET_ID_LEN];
  char alias[CODELET_TEXT_LEN];
  char pathHint[CODELET_TEXT_LEN];
  CodeletStatus state;
  int attentionPriority;
  int threadCount;
  ProjectCountsView counts;
  size_t recentThreadCount;
  size_t recentThreadTotal;
  ProjectThreadSummaryView recentThreads[CODELET_MAX_RECENT_THREADS_PER_PROJECT];
  int displayVersion;
};

struct ThreadView {
  char id[CODELET_ID_LEN];
  char projectId[CODELET_ID_LEN];
  char projectAlias[CODELET_TEXT_LEN];
  char source[CODELET_TEXT_LEN];
  char title[CODELET_TEXT_LEN];
  CodeletStatus status;
  int attentionPriority;
  int durationSec;
  char lastEvent[CODELET_LAST_EVENT_LEN];
  bool approvalRequired;
  int displayVersion;
};

struct CodeletSnapshot {
  int snapshotVersion;
  bool privacyMode;
  bool soundEnabled;
  bool muteActive;
  char serverStatus[24];
  char serverNow[40];
  char muteUntil[40];
  int projectCount;
  int threadCount;
  int needAttentionCount;
  size_t quotaBucketCount;
  size_t visibleProjectCount;
  size_t visibleThreadCount;
  QuotaBucketView quotaBuckets[CODELET_MAX_QUOTA_BUCKETS];
  ProjectView projects[CODELET_MAX_PROJECTS];
  ThreadView threads[CODELET_MAX_THREADS];
};

ParseResult parseSnapshotJson(const char *json, CodeletSnapshot &snapshot);
