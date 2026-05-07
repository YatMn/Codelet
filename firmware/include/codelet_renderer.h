#pragma once

#include "codelet_layout.h"
#include "codelet_snapshot.h"

enum class TextRole : uint8_t {
  Brand,
  ProjectTitle,
  PrimaryText,
  MetaText,
  BadgeText,
};

struct RenderStyle {
  bool hatched;
  bool dashedBorder;
  bool strongBorder;
};

enum class RenderMode : uint8_t {
  Full,
  Structural,
  Partial,
};

class DrawSink {
 public:
  virtual ~DrawSink() = default;
  virtual void clear() = 0;
  virtual void fillRect(Rect rect, uint8_t gray) = 0;
  virtual void clearRect(Rect rect) = 0;
  virtual void drawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint8_t gray) = 0;
  virtual void drawRect(Rect rect, RenderStyle style) = 0;
  virtual void drawText(Rect rect, const char *text, TextRole role, bool inverted = false) = 0;
  virtual void commit(Rect rect) = 0;
};

constexpr uint8_t GrayBlack = 0;
constexpr uint8_t GrayDark = 68;
constexpr uint8_t GrayMid = 136;
constexpr uint8_t GrayLine = 187;
constexpr uint8_t GrayPanel = 221;
constexpr uint8_t GrayPaper = 255;

Rect screenRect();
Rect contentRegion();

RenderStyle styleForStatus(CodeletStatus status);
void renderHome(DrawSink &sink, const Layout &layout, const CodeletSnapshot &snapshot,
                RenderMode mode = RenderMode::Full);
void renderThreads(DrawSink &sink, const Layout &layout, const CodeletSnapshot &snapshot,
                   RenderMode mode = RenderMode::Full);
void renderProjectDetail(DrawSink &sink, const Layout &layout, const CodeletSnapshot &snapshot, const char *projectId,
                         RenderMode mode = RenderMode::Full);
void renderStatusPage(DrawSink &sink, const Layout &layout, const char *state, const char *detail, const char *hint);
