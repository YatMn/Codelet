#include "codelet_app.h"

#include <string.h>

namespace {

bool contains(Rect rect, int16_t x, int16_t y) {
  return x >= rect.x && x < rect.x + rect.w && y >= rect.y && y < rect.y + rect.h;
}

bool isUrgent(CodeletStatus status) {
  return status == CodeletStatus::ApprovalRequired || status == CodeletStatus::Failed ||
         status == CodeletStatus::AwaitingUser;
}

bool isErrorPage(AppPage page) {
  return page == AppPage::Offline || page == AppPage::AuthFailed || page == AppPage::DataStale;
}

int absInt(int value) {
  return value < 0 ? -value : value;
}

bool isRightSwipe(TouchEvent event) {
  return event.flicked && event.distanceX >= 120 && absInt(event.distanceY) <= 80;
}

size_t minSize(size_t a, size_t b) {
  return a < b ? a : b;
}

void mixByte(uint32_t &hash, uint8_t value) {
  hash ^= value;
  hash *= 16777619UL;
}

void mixInt(uint32_t &hash, int value) {
  uint32_t bits = static_cast<uint32_t>(value);
  for (int i = 0; i < 4; i++) {
    mixByte(hash, static_cast<uint8_t>((bits >> (i * 8)) & 0xFF));
  }
}

void mixBool(uint32_t &hash, bool value) {
  mixByte(hash, value ? 1 : 0);
}

void mixString(uint32_t &hash, const char *value) {
  if (value != nullptr) {
    while (*value != '\0') {
      mixByte(hash, static_cast<uint8_t>(*value));
      value++;
    }
  }
  mixByte(hash, 0xFF);
}

void mixStatus(uint32_t &hash, CodeletStatus status) {
  mixByte(hash, static_cast<uint8_t>(status));
}

int displayDurationBucket(int durationSec) {
  if (durationSec < 0) {
    return -1;
  }
  int minutes = durationSec / 60;
  if (minutes < 60) {
    return minutes;
  }
  int hours = minutes / 60;
  if (hours < 24) {
    return 1000 + hours;
  }
  return 2000 + hours / 24;
}

void mixProjectCounts(uint32_t &hash, const ProjectCountsView &counts) {
  mixInt(hash, counts.approvalRequired);
  mixInt(hash, counts.failed);
  mixInt(hash, counts.awaitingUser);
  mixInt(hash, counts.running);
  mixInt(hash, counts.toolRunning);
  mixInt(hash, counts.longRunning);
  mixInt(hash, counts.stale);
  mixInt(hash, counts.completed);
}

void mixProjectRecentThread(uint32_t &hash, const ProjectThreadSummaryView &thread) {
  mixString(hash, thread.id);
  mixString(hash, thread.title);
  mixStatus(hash, thread.status);
}

size_t recentThreadTotalFor(const ProjectView &project) {
  return project.recentThreadTotal > project.recentThreadCount ? project.recentThreadTotal : project.recentThreadCount;
}

void mixQuotaFingerprint(uint32_t &hash, const CodeletSnapshot &snapshot) {
  size_t quotaCount = minSize(snapshot.quotaBucketCount, CODELET_MAX_QUOTA_BUCKETS);
  mixInt(hash, static_cast<int>(quotaCount));
  for (size_t i = 0; i < quotaCount; i++) {
    const QuotaBucketView &bucket = snapshot.quotaBuckets[i];
    mixString(hash, bucket.kind);
    mixString(hash, bucket.label);
    mixInt(hash, bucket.remainingPercent);
    mixString(hash, bucket.status);
  }
}

void mixHomeFingerprint(uint32_t &hash, const CodeletSnapshot &snapshot) {
  size_t projectCount = minSize(snapshot.visibleProjectCount, CODELET_MAX_PROJECTS);
  mixInt(hash, static_cast<int>(projectCount));
  for (size_t i = 0; i < projectCount; i++) {
    const ProjectView &project = snapshot.projects[i];
    mixString(hash, project.id);
    mixString(hash, project.alias);
    mixStatus(hash, project.state);
    mixInt(hash, project.threadCount);
    mixProjectCounts(hash, project.counts);
    size_t recentThreadCount = minSize(project.recentThreadCount, CODELET_MAX_RECENT_THREADS_PER_PROJECT);
    mixInt(hash, static_cast<int>(recentThreadTotalFor(project)));
    mixInt(hash, static_cast<int>(recentThreadCount));
    for (size_t j = 0; j < recentThreadCount; j++) {
      mixProjectRecentThread(hash, project.recentThreads[j]);
    }
  }
}

void mixThreadsFingerprint(uint32_t &hash, const CodeletSnapshot &snapshot) {
  size_t threadCount = minSize(snapshot.visibleThreadCount, CODELET_MAX_THREADS);
  mixInt(hash, static_cast<int>(threadCount));
  for (size_t i = 0; i < threadCount; i++) {
    const ThreadView &thread = snapshot.threads[i];
    mixString(hash, thread.id);
    mixString(hash, thread.projectAlias);
    mixString(hash, thread.title);
    mixStatus(hash, thread.status);
    mixString(hash, thread.lastEvent);
  }
}

void mixProjectDetailFingerprint(uint32_t &hash, const CodeletSnapshot &snapshot, const char *projectId) {
  size_t projectCount = minSize(snapshot.visibleProjectCount, CODELET_MAX_PROJECTS);
  for (size_t i = 0; i < projectCount; i++) {
    const ProjectView &project = snapshot.projects[i];
    if (projectId == nullptr || strcmp(project.id, projectId) != 0) {
      continue;
    }
    mixString(hash, project.id);
    mixString(hash, project.alias);
    mixStatus(hash, project.state);
    mixInt(hash, project.threadCount);
    mixProjectCounts(hash, project.counts);
    break;
  }

  size_t threadCount = minSize(snapshot.visibleThreadCount, CODELET_MAX_THREADS);
  int matchingCount = 0;
  for (size_t i = 0; i < threadCount; i++) {
    const ThreadView &thread = snapshot.threads[i];
    if (projectId == nullptr || strcmp(thread.projectId, projectId) != 0) {
      continue;
    }
    matchingCount++;
    mixString(hash, thread.id);
    mixString(hash, thread.title);
    mixStatus(hash, thread.status);
    mixString(hash, thread.lastEvent);
  }
  mixInt(hash, matchingCount);
}

uint32_t snapshotContentFingerprint(const CodeletSnapshot &snapshot, AppPage page, const char *projectId) {
  uint32_t hash = 2166136261UL;
  mixQuotaFingerprint(hash, snapshot);

  if (page == AppPage::Threads) {
    mixThreadsFingerprint(hash, snapshot);
  } else if (page == AppPage::ProjectDetail) {
    mixProjectDetailFingerprint(hash, snapshot, projectId);
  } else {
    mixHomeFingerprint(hash, snapshot);
  }

  return hash;
}

uint32_t urgentContentFingerprint(const CodeletSnapshot &snapshot) {
  uint32_t hash = 2166136261UL;
  bool hasUrgent = false;

  size_t projectCount = minSize(snapshot.visibleProjectCount, CODELET_MAX_PROJECTS);
  for (size_t i = 0; i < projectCount; i++) {
    const ProjectView &project = snapshot.projects[i];
    if (!isUrgent(project.state)) {
      continue;
    }
    hasUrgent = true;
    mixString(hash, project.id);
    mixString(hash, project.alias);
    mixStatus(hash, project.state);
    mixInt(hash, project.attentionPriority);
    mixInt(hash, project.threadCount);
    mixInt(hash, project.displayVersion);
  }

  size_t threadCount = minSize(snapshot.visibleThreadCount, CODELET_MAX_THREADS);
  for (size_t i = 0; i < threadCount; i++) {
    const ThreadView &thread = snapshot.threads[i];
    if (!isUrgent(thread.status) && !thread.approvalRequired) {
      continue;
    }
    hasUrgent = true;
    mixString(hash, thread.id);
    mixString(hash, thread.projectId);
    mixString(hash, thread.title);
    mixStatus(hash, thread.status);
    mixString(hash, thread.lastEvent);
    mixBool(hash, thread.approvalRequired);
    mixInt(hash, thread.displayVersion);
  }

  return hasUrgent ? hash : 0;
}

}  // namespace

ApplyResult CodeletApp::applySnapshot(const CodeletSnapshot &snapshot, uint32_t nowMs) {
  bool firstSnapshot = !hasSnapshot_;
  bool versionChanged = firstSnapshot || snapshot.snapshotVersion != lastSnapshotVersion_;

  snapshot_ = snapshot;
  bool recoveredFromErrorPage = false;
  bool structuralPageChanged = false;
  if (isErrorPage(page_)) {
    page_ = AppPage::Home;
    recoveredFromErrorPage = true;
  }

  if (page_ == AppPage::ProjectDetail) {
    bool projectStillVisible = false;
    size_t visibleCount = minSize(snapshot_.visibleProjectCount, CODELET_MAX_PROJECTS);
    for (size_t i = 0; i < visibleCount; i++) {
      if (strcmp(snapshot_.projects[i].id, selectedProjectId_) == 0) {
        projectStillVisible = true;
        break;
      }
    }
    if (!projectStillVisible) {
      selectedProjectId_[0] = '\0';
      page_ = AppPage::Home;
      structuralPageChanged = true;
    }
  }

  uint32_t newFingerprint = snapshotContentFingerprint(snapshot_, page_, selectedProjectId_);
  bool contentChanged = firstSnapshot || newFingerprint != contentFingerprint_;

  hasSnapshot_ = true;
  lastGoodSnapshotMs_ = nowMs;
  lastSnapshotVersion_ = snapshot.snapshotVersion;
  contentFingerprint_ = newFingerprint;

  RefreshMode refreshMode = RefreshMode::None;
  if (firstSnapshot || recoveredFromErrorPage) {
    refreshMode = RefreshMode::Full;
  } else if (structuralPageChanged) {
    refreshMode = RefreshMode::Structural;
  } else if (contentChanged) {
    refreshMode = RefreshMode::Partial;
  }
  pendingRefreshMode_ = refreshMode;

  return {refreshMode != RefreshMode::None, versionChanged, refreshMode};
}

bool CodeletApp::handleTouch(TouchEvent event, const Layout &layout) {
  if (isErrorPage(page_)) {
    return false;
  }

  if (isRightSwipe(event) && page_ == AppPage::ProjectDetail) {
    selectedProjectId_[0] = '\0';
    page_ = AppPage::Home;
    pendingRefreshMode_ = RefreshMode::Structural;
    return true;
  }

  if (!event.pressed) {
    return false;
  }

  if (contains(layout.navProjects, event.x, event.y)) {
    if (page_ == AppPage::Home) {
      return false;
    }
    selectedProjectId_[0] = '\0';
    page_ = AppPage::Home;
    pendingRefreshMode_ = RefreshMode::Structural;
    return true;
  }

  if (contains(layout.navThreads, event.x, event.y)) {
    if (page_ == AppPage::Threads) {
      return false;
    }
    page_ = AppPage::Threads;
    pendingRefreshMode_ = RefreshMode::Structural;
    return true;
  }

  if (page_ != AppPage::Home) {
    return false;
  }

  int layoutCardCount = layout.homeCardCount;
  if (layoutCardCount < 0) {
    layoutCardCount = 0;
  }
  size_t cardCount = minSize(snapshot_.visibleProjectCount, CODELET_MAX_PROJECTS);
  cardCount = minSize(cardCount, static_cast<size_t>(layoutCardCount));

  for (size_t i = 0; i < cardCount; i++) {
    if (!contains(layout.homeCards[i], event.x, event.y)) {
      continue;
    }
    strncpy(selectedProjectId_, snapshot_.projects[i].id, sizeof(selectedProjectId_) - 1);
    selectedProjectId_[sizeof(selectedProjectId_) - 1] = '\0';
    page_ = AppPage::ProjectDetail;
    pendingRefreshMode_ = RefreshMode::Structural;
    return true;
  }

  return false;
}

void CodeletApp::markRefreshed(uint32_t nowMs) {
  RefreshMode mode = pendingRefreshMode_;
  if (shouldFullRefresh(nowMs)) {
    mode = RefreshMode::Full;
  }
  markRefreshed(nowMs, mode);
}

void CodeletApp::markRefreshed(uint32_t nowMs, RefreshMode mode) {
  if (mode == RefreshMode::Full) {
    lastFullRefreshMs_ = nowMs;
    structuralRefreshCount_ = 0;
  } else if (mode == RefreshMode::Structural) {
    lastPartialRefreshMs_ = nowMs;
    if (structuralRefreshCount_ < CODELET_STRUCTURAL_REFRESHES_BEFORE_FULL) {
      structuralRefreshCount_++;
    }
  } else if (mode == RefreshMode::Partial) {
    lastPartialRefreshMs_ = nowMs;
  }
  pendingRefreshMode_ = RefreshMode::None;
}

bool CodeletApp::shouldFullRefresh(uint32_t nowMs) const {
  return nowMs - lastFullRefreshMs_ >= CODELET_FULL_REFRESH_INTERVAL_MS ||
         structuralRefreshCount_ >= CODELET_STRUCTURAL_REFRESHES_BEFORE_FULL;
}

bool CodeletApp::shouldBuzz(const CodeletSnapshot &snapshot, uint32_t nowMs) {
  (void)nowMs;
  if (!snapshot.soundEnabled || snapshot.muteActive) {
    return false;
  }

  uint32_t fingerprint = urgentContentFingerprint(snapshot);
  if (fingerprint == 0) {
    return false;
  }
  if (hasBuzzedUrgentContent_ && fingerprint == lastBuzzFingerprint_) {
    return false;
  }

  lastBuzzFingerprint_ = fingerprint;
  hasBuzzedUrgentContent_ = true;
  return true;
}

bool CodeletApp::setOfflinePage(AppPage page) {
  if (page_ == page) {
    return false;
  }
  page_ = page;
  pendingRefreshMode_ = RefreshMode::Full;
  return true;
}

bool CodeletApp::hasSnapshot() const {
  return hasSnapshot_;
}

bool CodeletApp::isDataStale(uint32_t nowMs) const {
  return hasSnapshot_ && nowMs - lastGoodSnapshotMs_ >= CODELET_DATA_STALE_MS;
}

bool CodeletApp::shouldShowConnectionFailure(uint32_t nowMs) const {
  return !hasSnapshot_ || isDataStale(nowMs);
}

AppPage CodeletApp::connectionFailurePage(uint32_t nowMs) const {
  return isDataStale(nowMs) ? AppPage::DataStale : AppPage::Offline;
}

RefreshMode CodeletApp::pendingRefreshMode() const {
  return pendingRefreshMode_;
}

AppPage CodeletApp::page() const {
  return page_;
}

const char *CodeletApp::selectedProjectId() const {
  return selectedProjectId_;
}

const CodeletSnapshot &CodeletApp::snapshot() const {
  return snapshot_;
}
