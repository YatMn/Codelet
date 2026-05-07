#pragma once

#include "codelet_config.h"
#include "codelet_layout.h"
#include "codelet_snapshot.h"

enum class AppPage : uint8_t {
  Home,
  Threads,
  ProjectDetail,
  Offline,
  AuthFailed,
  DataStale,
};

struct TouchEvent {
  int16_t x = 0;
  int16_t y = 0;
  bool pressed = false;
  bool flicked = false;
  int16_t distanceX = 0;
  int16_t distanceY = 0;
};

enum class RefreshMode : uint8_t {
  None,
  Partial,
  Structural,
  Full,
};

struct ApplyResult {
  bool needsRefresh;
  bool versionChanged;
  RefreshMode refreshMode;
};

class CodeletApp {
 public:
  ApplyResult applySnapshot(const CodeletSnapshot &snapshot, uint32_t nowMs);
  bool handleTouch(TouchEvent event, const Layout &layout);
  void markRefreshed(uint32_t nowMs);
  void markRefreshed(uint32_t nowMs, RefreshMode mode);
  bool shouldFullRefresh(uint32_t nowMs) const;
  bool shouldBuzz(const CodeletSnapshot &snapshot, uint32_t nowMs);
  bool setOfflinePage(AppPage page);
  bool hasSnapshot() const;
  bool isDataStale(uint32_t nowMs) const;
  bool shouldShowConnectionFailure(uint32_t nowMs) const;
  AppPage connectionFailurePage(uint32_t nowMs) const;
  RefreshMode pendingRefreshMode() const;

  AppPage page() const;
  const char *selectedProjectId() const;
  const CodeletSnapshot &snapshot() const;

 private:
  CodeletSnapshot snapshot_{};
  AppPage page_ = AppPage::Home;
  char selectedProjectId_[CODELET_ID_LEN] = "";
  bool hasSnapshot_ = false;
  int lastSnapshotVersion_ = -1;
  uint32_t lastFullRefreshMs_ = 0;
  uint32_t lastPartialRefreshMs_ = 0;
  uint8_t structuralRefreshCount_ = 0;
  uint32_t lastGoodSnapshotMs_ = 0;
  uint32_t contentFingerprint_ = 0;
  uint32_t lastBuzzFingerprint_ = 0;
  RefreshMode pendingRefreshMode_ = RefreshMode::None;
  bool hasBuzzedUrgentContent_ = false;
};
