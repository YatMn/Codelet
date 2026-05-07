#include "codelet_snapshot.h"

#include <ArduinoJson.h>
#include <string.h>

namespace {

void copyString(char *dest, size_t destSize, const char *value) {
  if (destSize == 0) {
    return;
  }
  if (value == nullptr) {
    dest[0] = '\0';
    return;
  }
  strncpy(dest, value, destSize - 1);
  dest[destSize - 1] = '\0';
}

int jsonIntOr(JsonVariantConst value, int fallback) {
  if (value.isNull()) {
    return fallback;
  }
  return value.as<int>();
}

int attentionPriorityFor(JsonObjectConst object, CodeletStatus status) {
  JsonVariantConst value = object["attention_priority"];
  return jsonIntOr(value, priorityForStatus(status));
}

void parseProjectCounts(JsonObjectConst counts, ProjectCountsView &view) {
  view.approvalRequired = counts["approval_required"] | 0;
  view.failed = counts["failed"] | 0;
  view.awaitingUser = counts["awaiting_user"] | 0;
  view.running = counts["running"] | 0;
  view.toolRunning = counts["tool_running"] | 0;
  view.longRunning = counts["long_running"] | 0;
  view.stale = counts["stale"] | 0;
  view.completed = counts["completed"] | 0;
}

void parseProjectThreadSummary(JsonObjectConst thread, ProjectThreadSummaryView &view) {
  view.status = parseStatus(thread["status"] | "");
  copyString(view.id, sizeof(view.id), thread["id"] | "");
  copyString(view.title, sizeof(view.title), thread["title"] | "");
  view.attentionPriority = attentionPriorityFor(thread, view.status);
  view.durationSec = jsonIntOr(thread["duration_sec"], -1);
  copyString(view.lastEvent, sizeof(view.lastEvent), thread["last_event"] | "");
  view.approvalRequired = thread["approval_required"] | false;
  view.displayVersion = thread["display_version"] | 0;
}

void parseRecentThreads(JsonObjectConst project, ProjectView &view) {
  JsonArrayConst recentThreads = project["recent_threads"].as<JsonArrayConst>();
  size_t index = 0;
  size_t total = 0;
  for (JsonObjectConst thread : recentThreads) {
    if (index < CODELET_MAX_RECENT_THREADS_PER_PROJECT) {
      parseProjectThreadSummary(thread, view.recentThreads[index]);
      index++;
    }
    total++;
  }
  view.recentThreadCount = index;
  view.recentThreadTotal = total;
}

void parseQuotaBuckets(JsonVariantConst quotaValue, CodeletSnapshot &snapshot) {
  JsonArrayConst buckets = quotaValue["buckets"].as<JsonArrayConst>();
  size_t index = 0;
  for (JsonObjectConst bucket : buckets) {
    if (index >= CODELET_MAX_QUOTA_BUCKETS) {
      break;
    }

    QuotaBucketView &view = snapshot.quotaBuckets[index];
    copyString(view.kind, sizeof(view.kind), bucket["kind"] | "");
    copyString(view.label, sizeof(view.label), bucket["label"] | "");
    view.usedPercent = bucket["used_percent"] | 0;
    view.remainingPercent = bucket["remaining_percent"] | 0;
    view.resetInSec = jsonIntOr(bucket["reset_in_sec"], -1);
    copyString(view.status, sizeof(view.status), bucket["status"] | "");
    index++;
  }
  snapshot.quotaBucketCount = index;
}

void parseProjects(const JsonDocument &doc, CodeletSnapshot &snapshot) {
  JsonArrayConst projects = doc["projects"].as<JsonArrayConst>();
  size_t index = 0;
  for (JsonObjectConst project : projects) {
    if (index >= CODELET_MAX_PROJECTS) {
      break;
    }

    ProjectView &view = snapshot.projects[index];
    view.state = parseStatus(project["state"] | "");
    copyString(view.id, sizeof(view.id), project["id"] | "");
    copyString(view.alias, sizeof(view.alias), project["alias"] | "");
    copyString(view.pathHint, sizeof(view.pathHint), project["path_hint"] | "");
    view.attentionPriority = attentionPriorityFor(project, view.state);
    view.threadCount = project["thread_count"] | 0;
    parseProjectCounts(project["counts"].as<JsonObjectConst>(), view.counts);
    parseRecentThreads(project, view);
    view.displayVersion = project["display_version"] | 0;
    index++;
  }
  snapshot.visibleProjectCount = index;
}

const char *projectAliasFor(const CodeletSnapshot &snapshot, const char *projectId) {
  if (projectId == nullptr || projectId[0] == '\0') {
    return "";
  }

  size_t visibleCount = snapshot.visibleProjectCount;
  if (visibleCount > CODELET_MAX_PROJECTS) {
    visibleCount = CODELET_MAX_PROJECTS;
  }

  for (size_t i = 0; i < visibleCount; i++) {
    const ProjectView &project = snapshot.projects[i];
    if (strcmp(project.id, projectId) == 0 && project.alias[0] != '\0') {
      return project.alias;
    }
  }

  return projectId;
}

void parseThreads(const JsonDocument &doc, CodeletSnapshot &snapshot) {
  JsonArrayConst threads = doc["threads"].as<JsonArrayConst>();
  size_t index = 0;
  for (JsonObjectConst thread : threads) {
    if (index >= CODELET_MAX_THREADS) {
      break;
    }

    ThreadView &view = snapshot.threads[index];
    view.status = parseStatus(thread["status"] | "");
    copyString(view.id, sizeof(view.id), thread["id"] | "");
    copyString(view.projectId, sizeof(view.projectId), thread["project_id"] | "");
    copyString(view.projectAlias, sizeof(view.projectAlias), projectAliasFor(snapshot, view.projectId));
    copyString(view.source, sizeof(view.source), thread["source"] | "");
    copyString(view.title, sizeof(view.title), thread["title"] | "");
    view.attentionPriority = attentionPriorityFor(thread, view.status);
    view.durationSec = jsonIntOr(thread["duration_sec"], -1);
    copyString(view.lastEvent, sizeof(view.lastEvent), thread["last_event"] | "");
    view.approvalRequired = thread["approval_required"] | false;
    view.displayVersion = thread["display_version"] | 0;
    index++;
  }
  snapshot.visibleThreadCount = index;
}

}  // namespace

ParseResult parseSnapshotJson(const char *json, CodeletSnapshot &snapshot) {
  if (json == nullptr || strlen(json) >= CODELET_MAX_SNAPSHOT_JSON_BYTES) {
    return {ParseStatus::InvalidJson, "snapshot too large"};
  }

  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, json);
  if (error) {
    return {ParseStatus::InvalidJson, error.c_str()};
  }

  JsonObjectConst server = doc["server"].as<JsonObjectConst>();
  if (server.isNull()) {
    return {ParseStatus::MissingServer, "missing server"};
  }

  memset(&snapshot, 0, sizeof(snapshot));

  snapshot.snapshotVersion = server["snapshot_version"] | 0;
  snapshot.privacyMode = server["privacy_mode"] | false;
  snapshot.soundEnabled = server["sound_enabled"] | false;
  JsonVariantConst muteUntil = server["mute_until"];
  copyString(snapshot.muteUntil, sizeof(snapshot.muteUntil), muteUntil | "");
  snapshot.muteActive = muteUntil.isNull() ? false : snapshot.muteUntil[0] != '\0';
  copyString(snapshot.serverStatus, sizeof(snapshot.serverStatus), server["status"] | "");
  copyString(snapshot.serverNow, sizeof(snapshot.serverNow), server["now"] | "");

  JsonObjectConst summary = doc["summary"].as<JsonObjectConst>();
  snapshot.projectCount = summary["project_count"] | 0;
  snapshot.threadCount = summary["thread_count"] | 0;
  snapshot.needAttentionCount = summary["need_attention_count"] | 0;

  parseQuotaBuckets(doc["quota"], snapshot);
  parseProjects(doc, snapshot);
  parseThreads(doc, snapshot);

  return {ParseStatus::Ok, "ok"};
}
