#include "codelet_renderer.h"

#include <stdio.h>
#include <string.h>

#include "codelet_config.h"

namespace {

size_t minSize(size_t a, size_t b) {
  return a < b ? a : b;
}

int clampPercent(int value) {
  if (value < 0) return 0;
  if (value > 100) return 100;
  return value;
}

int asciiUnitWidth(TextRole role) {
  switch (role) {
    case TextRole::Brand:
    case TextRole::ProjectTitle:
      return 12;
    case TextRole::PrimaryText:
    case TextRole::BadgeText:
      return 8;
    case TextRole::MetaText:
      return 8;
  }
  return 8;
}

int cjkUnitWidth(TextRole role) {
  switch (role) {
    case TextRole::Brand:
    case TextRole::ProjectTitle:
      return 24;
    case TextRole::PrimaryText:
    case TextRole::BadgeText:
      return 16;
    case TextRole::MetaText:
      return 16;
  }
  return 16;
}

int utf8CharBytes(unsigned char firstByte) {
  if ((firstByte & 0x80) == 0) return 1;
  if ((firstByte & 0xE0) == 0xC0) return 2;
  if ((firstByte & 0xF0) == 0xE0) return 3;
  if ((firstByte & 0xF8) == 0xF0) return 4;
  return 1;
}

int glyphPixelWidth(unsigned char firstByte, TextRole role) {
  return (firstByte & 0x80) == 0 ? asciiUnitWidth(role) : cjkUnitWidth(role);
}

int textPixelWidth(const char *text, TextRole role) {
  if (text == nullptr) return 0;
  int pixels = 0;
  const char *cursor = text;
  while (*cursor != '\0') {
    unsigned char first = static_cast<unsigned char>(*cursor);
    pixels += glyphPixelWidth(first, role);
    cursor += utf8CharBytes(first);
  }
  return pixels;
}

size_t clippedText(const char *text, Rect rect, TextRole role, char *buffer, size_t bufferSize) {
  if (bufferSize == 0) return 0;
  buffer[0] = '\0';
  if (text == nullptr || text[0] == '\0') return 0;

  int capacity = rect.w - 12;
  int suffixWidth = textPixelWidth("..", role);
  if (capacity < suffixWidth) capacity = suffixWidth;

  int used = 0;
  size_t written = 0;
  int glyphWidths[128];
  size_t glyphEnds[128];
  size_t glyphCount = 0;
  const char *cursor = text;
  bool clipped = false;
  while (*cursor != '\0') {
    unsigned char first = static_cast<unsigned char>(*cursor);
    int charBytes = utf8CharBytes(first);
    int pixels = glyphPixelWidth(first, role);
    if (used + pixels > capacity || written + static_cast<size_t>(charBytes) >= bufferSize) {
      clipped = true;
      break;
    }
    for (int i = 0; i < charBytes && cursor[i] != '\0'; i++) {
      buffer[written++] = cursor[i];
    }
    used += pixels;
    if (glyphCount < 128) {
      glyphWidths[glyphCount] = pixels;
      glyphEnds[glyphCount] = written;
      glyphCount++;
    }
    cursor += charBytes;
  }

  if (!clipped && *cursor == '\0') {
    buffer[written] = '\0';
    return written;
  }

  while (glyphCount > 0 && used + suffixWidth > capacity) {
    glyphCount--;
    used -= glyphWidths[glyphCount];
    written = glyphCount == 0 ? 0 : glyphEnds[glyphCount - 1];
  }
  if (written + 2 >= bufferSize) {
    written = bufferSize > 3 ? bufferSize - 3 : 0;
  }
  buffer[written++] = '.';
  buffer[written++] = '.';
  buffer[written] = '\0';
  return written;
}

void drawClippedText(DrawSink &sink, Rect rect, const char *text, TextRole role, bool inverted = false) {
  char buffer[128];
  clippedText(text, rect, role, buffer, sizeof(buffer));
  sink.drawText(rect, buffer, role, inverted);
}

void drawCenteredText(DrawSink &sink, Rect rect, const char *text, TextRole role, bool inverted = false) {
  int textWidth = textPixelWidth(text, role) + 12;
  int16_t x = rect.x;
  int16_t w = rect.w;
  if (textWidth < rect.w) {
    x = static_cast<int16_t>(rect.x + (rect.w - textWidth) / 2);
    w = static_cast<int16_t>(textWidth);
  }
  drawClippedText(sink, {x, rect.y, w, rect.h}, text, role, inverted);
}

bool stringsEqual(const char *left, const char *right) {
  if (left == nullptr || right == nullptr) return left == right;
  return strcmp(left, right) == 0;
}

bool isPartial(RenderMode mode) {
  return mode == RenderMode::Partial;
}

bool isStructural(RenderMode mode) {
  return mode == RenderMode::Structural;
}

bool isFull(RenderMode mode) {
  return mode == RenderMode::Full;
}

size_t recentThreadTotalFor(const ProjectView &project) {
  return project.recentThreadTotal > project.recentThreadCount ? project.recentThreadTotal : project.recentThreadCount;
}

void prepareFrame(DrawSink &sink, RenderMode mode) {
  if (isFull(mode)) {
    sink.clear();
    return;
  }
  if (isStructural(mode)) {
    sink.fillRect(contentRegion(), GrayPaper);
    sink.commit(contentRegion());
  }
}

Rect insetRect(Rect rect, int16_t left, int16_t top, int16_t right, int16_t height) {
  return {static_cast<int16_t>(rect.x + left),
          static_cast<int16_t>(rect.y + top),
          static_cast<int16_t>(rect.w - left - right),
          height};
}

Rect columnRect(Rect row, int16_t x, int16_t w) {
  return {static_cast<int16_t>(row.x + x), row.y, w, row.h};
}

Rect rowRect(Rect area, int index, int rowStep, int rowHeight) {
  return {area.x, static_cast<int16_t>(area.y + index * rowStep), area.w, static_cast<int16_t>(rowHeight)};
}

Rect unionRect(Rect a, Rect b) {
  int16_t left = a.x < b.x ? a.x : b.x;
  int16_t top = a.y < b.y ? a.y : b.y;
  int16_t right = a.x + a.w > b.x + b.w ? a.x + a.w : b.x + b.w;
  int16_t bottom = a.y + a.h > b.y + b.h ? a.y + a.h : b.y + b.h;
  return {left, top, static_cast<int16_t>(right - left), static_cast<int16_t>(bottom - top)};
}

Rect homeCardsUnion(const Layout &layout, int layoutCardCount) {
  if (layoutCardCount <= 0) {
    return {0, 0, 0, 0};
  }

  int cardCount = layoutCardCount;
  if (cardCount > 6) {
    cardCount = 6;
  }

  Rect result = layout.homeCards[0];
  for (int i = 1; i < cardCount; i++) {
    result = unionRect(result, layout.homeCards[i]);
  }
  return result;
}

Rect rowTextRect(Rect row, int16_t x, int16_t w, int16_t verticalInset = 5) {
  return {static_cast<int16_t>(row.x + x),
          static_cast<int16_t>(row.y + verticalInset),
          w,
          static_cast<int16_t>(row.h - verticalInset * 2)};
}

void drawHLine(DrawSink &sink, int16_t x, int16_t y, int16_t w, uint8_t gray = GrayBlack) {
  sink.drawLine(x, y, static_cast<int16_t>(x + w), y, gray);
}

void drawVLine(DrawSink &sink, int16_t x, int16_t y, int16_t h, uint8_t gray = GrayBlack) {
  sink.drawLine(x, y, x, static_cast<int16_t>(y + h), gray);
}

void drawBar(DrawSink &sink, Rect rect, int usedPercent) {
  int used = clampPercent(usedPercent);
  sink.fillRect(rect, GrayPaper);
  sink.drawRect(rect, {false, false, false});
  int fillWidth = (rect.w - 2) * used / 100;
  if (fillWidth > 0) {
    Rect fill = {static_cast<int16_t>(rect.x + 1), static_cast<int16_t>(rect.y + 1),
                 static_cast<int16_t>(fillWidth), static_cast<int16_t>(rect.h - 2)};
    sink.fillRect(fill, GrayDark);
    for (int16_t x = fill.x + 4; x < fill.x + fill.w; x += 7) {
      drawVLine(sink, x, fill.y, fill.h, GrayBlack);
    }
  }
}

void formatDuration(int durationSec, char *buffer, size_t bufferSize) {
  if (bufferSize == 0) return;
  if (durationSec < 0) {
    snprintf(buffer, bufferSize, "--");
    return;
  }
  int minutes = durationSec / 60;
  if (minutes < 60) {
    snprintf(buffer, bufferSize, "%dm", minutes);
    return;
  }
  int hours = minutes / 60;
  if (hours < 24) {
    snprintf(buffer, bufferSize, "%dh", hours);
    return;
  }
  snprintf(buffer, bufferSize, "%dd", hours / 24);
}

void formatReset(int resetInSec, char *buffer, size_t bufferSize) {
  if (bufferSize == 0) return;
  if (resetInSec <= 0) {
    snprintf(buffer, bufferSize, "--");
    return;
  }
  int minutes = resetInSec / 60;
  if (minutes < 60) {
    snprintf(buffer, bufferSize, "%dm", minutes);
    return;
  }
  int hours = minutes / 60;
  int remMinutes = minutes % 60;
  if (hours < 24 && remMinutes > 0) {
    snprintf(buffer, bufferSize, "%dh%dm", hours, remMinutes);
    return;
  }
  if (hours < 24) {
    snprintf(buffer, bufferSize, "%dh", hours);
    return;
  }
  snprintf(buffer, bufferSize, "%dd", hours / 24);
}

const char *primaryQuotaLabel(const CodeletSnapshot &snapshot) {
  if (snapshot.quotaBucketCount == 0 || snapshot.quotaBuckets[0].label[0] == '\0') {
    return "--";
  }
  return snapshot.quotaBuckets[0].label;
}

bool batteryPercentIsKnown(DevicePowerStatus power) {
  return power.known && power.batteryPercent >= 0 && power.batteryPercent <= 100;
}

void formatBatteryPercent(DevicePowerStatus power, char *buffer, size_t bufferSize) {
  if (bufferSize == 0) return;
  if (!batteryPercentIsKnown(power)) {
    snprintf(buffer, bufferSize, "--%%");
    return;
  }
  snprintf(buffer, bufferSize, "%d%%", power.batteryPercent);
}

void drawBatteryIndicator(DrawSink &sink, Rect slot, DevicePowerStatus power) {
  sink.fillRect(slot, GrayPaper);

  Rect outline = {slot.x, static_cast<int16_t>(slot.y + 2), 62, 16};
  Rect cap = {static_cast<int16_t>(outline.x + outline.w), static_cast<int16_t>(outline.y + 5), 4, 6};
  sink.drawRect(outline, {false, false, false});
  sink.fillRect(cap, GrayBlack);

  if (batteryPercentIsKnown(power)) {
    int percent = clampPercent(power.batteryPercent);
    int fillWidth = (outline.w - 4) * percent / 100;
    if (fillWidth > 0) {
      Rect fill = {static_cast<int16_t>(outline.x + 2), static_cast<int16_t>(outline.y + 2),
                   static_cast<int16_t>(fillWidth), static_cast<int16_t>(outline.h - 4)};
      sink.fillRect(fill, GrayBlack);
      if (power.charging) {
        for (int16_t x = fill.x + 4; x < fill.x + fill.w; x += 8) {
          sink.drawLine(x, static_cast<int16_t>(fill.y + fill.h), static_cast<int16_t>(x + 6), fill.y, GrayPaper);
        }
      }
    }
  }

  char percent[8];
  formatBatteryPercent(power, percent, sizeof(percent));
  drawClippedText(sink, {static_cast<int16_t>(slot.x + 68), slot.y, 52, 22}, percent, TextRole::MetaText);
}

const char *prototypeStatusLabel(CodeletStatus status) {
  switch (status) {
    case CodeletStatus::ApprovalRequired: return "! ASK";
    case CodeletStatus::Failed: return "X FAIL";
    case CodeletStatus::AwaitingUser: return "? WAIT";
    case CodeletStatus::Stale: return "~ STALE";
    case CodeletStatus::LongRunning: return "> LONG";
    case CodeletStatus::ToolRunning: return "> TOOL";
    case CodeletStatus::Running: return "> RUN";
    case CodeletStatus::Thinking: return "...";
    case CodeletStatus::Completed: return "v DONE";
    case CodeletStatus::Idle: return "- IDLE";
    case CodeletStatus::Cancelled: return "/ STOP";
    case CodeletStatus::Unknown: return "?";
  }
  return "?";
}

void appendCount(char *line, size_t lineSize, int count, const char *label) {
  if (count <= 0 || lineSize == 0) return;
  size_t used = strlen(line);
  if (used >= lineSize - 1) return;
  const char *separator = used == 0 ? "" : " . ";
  snprintf(line + used, lineSize - used, "%s%d %s", separator, count, label);
}

void renderQuota(DrawSink &sink, const Layout &layout, const CodeletSnapshot &snapshot, RenderMode mode) {
  (void)mode;
  sink.fillRect(layout.quota, GrayPanel);
  sink.drawRect(layout.quota, {false, false, false});

  char primary[40];
  snprintf(primary, sizeof(primary), "PRIMARY: %s", primaryQuotaLabel(snapshot));
  drawClippedText(sink, insetRect(layout.quota, 12, 8, 144, 18), "QUOTA", TextRole::MetaText);
  drawClippedText(sink, insetRect(layout.quota, 120, 8, 12, 18), primary, TextRole::MetaText);
  drawHLine(sink, static_cast<int16_t>(layout.quota.x + 12), static_cast<int16_t>(layout.quota.y + 32),
            static_cast<int16_t>(layout.quota.w - 24), GrayMid);

  size_t quotaCount = minSize(snapshot.quotaBucketCount, CODELET_MAX_QUOTA_BUCKETS);
  if (quotaCount == 0) {
    drawClippedText(sink, insetRect(layout.quota, 12, 46, 12, 22), "quota --", TextRole::MetaText);
    return;
  }

  for (size_t i = 0; i < quotaCount; i++) {
    const QuotaBucketView &bucket = snapshot.quotaBuckets[i];
    int16_t y = static_cast<int16_t>(layout.quota.y + 42 + static_cast<int>(i) * 26);
    const char *label = bucket.label[0] == '\0' ? bucket.kind : bucket.label;
    char reset[16];
    if (stringsEqual(bucket.status, "unknown")) {
      snprintf(reset, sizeof(reset), "--");
    } else {
      formatReset(bucket.resetInSec, reset, sizeof(reset));
    }
    drawClippedText(sink, {static_cast<int16_t>(layout.quota.x + 12), y, 48, 20}, label, TextRole::MetaText);
    drawBar(sink, {static_cast<int16_t>(layout.quota.x + 70), static_cast<int16_t>(y + 4), 134, 12},
            stringsEqual(bucket.status, "unknown") ? 0 : bucket.usedPercent);
    drawClippedText(sink, {static_cast<int16_t>(layout.quota.x + 214), y, 54, 20}, reset, TextRole::MetaText);
  }
}

void renderTopChrome(DrawSink &sink, const Layout &layout, const CodeletSnapshot &snapshot, const char *activeLabel,
                     RenderMode mode, DevicePowerStatus power) {
  if (!isFull(mode)) {
    sink.fillRect(layout.header, GrayPaper);
  }

  drawClippedText(sink, {layout.header.x, static_cast<int16_t>(layout.header.y + 2), 244, 58}, "Codelet",
                  TextRole::Brand);
  drawClippedText(sink, {static_cast<int16_t>(layout.header.x + 254), static_cast<int16_t>(layout.header.y + 12),
                         110, 22},
                  CODELET_FIRMWARE_LABEL, TextRole::MetaText);
  drawBatteryIndicator(sink, {static_cast<int16_t>(layout.header.x + 254),
                              static_cast<int16_t>(layout.header.y + 42), 128, 22},
                       power);
  char summary[88];
  snprintf(summary, sizeof(summary), "%d projects . %d sessions . %d need attention",
           snapshot.projectCount, snapshot.threadCount, snapshot.needAttentionCount);
  drawClippedText(sink, {layout.header.x, static_cast<int16_t>(layout.header.y + 74), 360, 22}, summary,
                  TextRole::MetaText);
  drawHLine(sink, layout.header.x, static_cast<int16_t>(layout.header.y + layout.header.h),
            layout.header.w, GrayBlack);

  bool projectsActive = stringsEqual(activeLabel, "Projects");
  bool threadsActive = stringsEqual(activeLabel, "Threads");
  if (projectsActive) sink.fillRect(layout.navProjects, GrayBlack);
  if (threadsActive) sink.fillRect(layout.navThreads, GrayBlack);
  sink.drawRect(layout.navProjects, {false, false, false});
  sink.drawRect(layout.navThreads, {false, false, false});
  drawCenteredText(sink, layout.navProjects, "PROJECT", TextRole::BadgeText, projectsActive);
  drawCenteredText(sink, layout.navThreads, "THREAD", TextRole::BadgeText, threadsActive);
}

uint8_t panelGrayForStatus(CodeletStatus status) {
  switch (status) {
    case CodeletStatus::ApprovalRequired:
    case CodeletStatus::Failed:
    case CodeletStatus::AwaitingUser:
      return GrayPanel;
    default:
      return GrayPaper;
  }
}

void renderProjectCounts(DrawSink &sink, Rect card, const ProjectView &project) {
  char line[96] = "";
  char total[24];
  snprintf(total, sizeof(total), "%d threads", project.threadCount);
  strncat(line, total, sizeof(line) - strlen(line) - 1);
  appendCount(line, sizeof(line), project.counts.approvalRequired, "ask");
  appendCount(line, sizeof(line), project.counts.failed, "fail");
  appendCount(line, sizeof(line), project.counts.awaitingUser, "wait");
  appendCount(line, sizeof(line), project.counts.longRunning, "long");
  appendCount(line, sizeof(line), project.counts.toolRunning, "tool");
  appendCount(line, sizeof(line), project.counts.running, "run");
  appendCount(line, sizeof(line), project.counts.completed, "done");
  drawClippedText(sink, insetRect(card, 14, 58, 14, 24), line, TextRole::MetaText);
}

void renderProjectThreadSummary(DrawSink &sink, Rect card, const ProjectView &project) {
  size_t availableCount = minSize(project.recentThreadCount, CODELET_MAX_RECENT_THREADS_PER_PROJECT);
  size_t totalCount = recentThreadTotalFor(project);
  if (availableCount == 0) {
    return;
  }

  drawHLine(sink, static_cast<int16_t>(card.x + 14), static_cast<int16_t>(card.y + 92),
            static_cast<int16_t>(card.w - 28), GrayMid);

  size_t visibleRows = minSize(availableCount, static_cast<size_t>(2));
  for (size_t i = 0; i < visibleRows; i++) {
    const ProjectThreadSummaryView &thread = project.recentThreads[i];
    char age[12];
    formatDuration(thread.durationSec, age, sizeof(age));
    Rect titleRow = insetRect(card, 14, static_cast<int16_t>(102 + static_cast<int>(i) * 28), 14, 28);
    drawClippedText(sink, columnRect(titleRow, 0, static_cast<int16_t>(titleRow.w - 58)),
                    thread.title, TextRole::PrimaryText);
    drawClippedText(sink, columnRect(titleRow, static_cast<int16_t>(titleRow.w - 48), 48),
                    age, TextRole::MetaText);
  }

  if (totalCount > 2) {
    char more[16];
    snprintf(more, sizeof(more), "+%d more", static_cast<int>(totalCount - 2));
    drawClippedText(sink, insetRect(card, 14, 158, 14, 22), more, TextRole::MetaText);
  }
}

void renderHomeCard(DrawSink &sink, Rect card, const ProjectView *project, RenderMode mode) {
  (void)mode;
  if (project == nullptr) {
    sink.fillRect(card, GrayPaper);
    sink.drawRect(card, {false, false, false});
    return;
  }

  sink.fillRect(card, panelGrayForStatus(project->state));
  sink.drawRect(card, styleForStatus(project->state));
  Rect badge = {static_cast<int16_t>(card.x + card.w - 126), static_cast<int16_t>(card.y + 12), 108, 24};
  drawClippedText(sink, badge, prototypeStatusLabel(project->state), TextRole::BadgeText, true);
  drawClippedText(sink, insetRect(card, 14, 18, 150, 34), project->alias, TextRole::ProjectTitle);
  renderProjectCounts(sink, card, *project);
  renderProjectThreadSummary(sink, card, *project);
}

Rect threadsBody(Rect table) {
  return {static_cast<int16_t>(table.x + 16),
          static_cast<int16_t>(table.y + 50),
          static_cast<int16_t>(table.w - 32),
          static_cast<int16_t>(table.h - 64)};
}

void renderThreadsHeader(DrawSink &sink, Rect table) {
  Rect header = {static_cast<int16_t>(table.x + 16), static_cast<int16_t>(table.y + 16),
                 static_cast<int16_t>(table.w - 32), 24};
  drawClippedText(sink, columnRect(header, 0, 92), "STATUS", TextRole::MetaText);
  drawClippedText(sink, columnRect(header, 108, 128), "PROJECT", TextRole::MetaText);
  drawClippedText(sink, columnRect(header, 250, 410), "TITLE", TextRole::MetaText);
  drawClippedText(sink, columnRect(header, 680, 58), "AGE", TextRole::MetaText);
  drawClippedText(sink, columnRect(header, 752, static_cast<int16_t>(header.w - 752)), "LAST", TextRole::MetaText);
  drawHLine(sink, header.x, static_cast<int16_t>(header.y + header.h + 6), header.w, GrayBlack);
}

int maxRowsFor(Rect area, int rowStep, int rowHeight) {
  if (rowStep <= 0 || rowHeight <= 0 || area.h < rowHeight) return 0;
  return 1 + (area.h - rowHeight) / rowStep;
}

void renderThreadTableRow(DrawSink &sink, Rect row, const ThreadView &thread) {
  sink.fillRect(row, panelGrayForStatus(thread.status));
  char age[12];
  formatDuration(thread.durationSec, age, sizeof(age));

  Rect textRow = {static_cast<int16_t>(row.x + 8), static_cast<int16_t>(row.y + 10),
                  static_cast<int16_t>(row.w - 16), 34};
  drawClippedText(sink, columnRect(textRow, 0, 92), prototypeStatusLabel(thread.status), TextRole::BadgeText);
  drawClippedText(sink, columnRect(textRow, 108, 128), thread.projectAlias, TextRole::MetaText);
  drawClippedText(sink, columnRect(textRow, 250, 410), thread.title, TextRole::PrimaryText);
  drawClippedText(sink, columnRect(textRow, 680, 58), age, TextRole::MetaText);
  drawClippedText(sink, columnRect(textRow, 752, static_cast<int16_t>(textRow.w - 752)),
                  thread.lastEvent, TextRole::MetaText);

  drawHLine(sink, row.x, static_cast<int16_t>(row.y + row.h), row.w, GrayLine);
  if (usesHatchedBackground(thread.status) || usesDashedBorder(thread.status)) {
    sink.drawRect(row, styleForStatus(thread.status));
  }
}

void renderProjectDetailRow(DrawSink &sink, Rect row, const ThreadView &thread) {
  sink.fillRect(row, panelGrayForStatus(thread.status));

  char age[12];
  formatDuration(thread.durationSec, age, sizeof(age));

  Rect textRow = {static_cast<int16_t>(row.x + 8), static_cast<int16_t>(row.y + 10),
                  static_cast<int16_t>(row.w - 16), 34};
  drawClippedText(sink, columnRect(textRow, 0, 104), prototypeStatusLabel(thread.status), TextRole::BadgeText);
  drawClippedText(sink, columnRect(textRow, 120, 500), thread.title, TextRole::PrimaryText);
  drawClippedText(sink, columnRect(textRow, 638, 58), age, TextRole::MetaText);
  drawClippedText(sink, columnRect(textRow, 712, static_cast<int16_t>(textRow.w - 712)),
                  thread.lastEvent, TextRole::MetaText);

  drawHLine(sink, row.x, static_cast<int16_t>(row.y + row.h), row.w, GrayLine);
  if (usesHatchedBackground(thread.status) || usesDashedBorder(thread.status)) {
    sink.drawRect(row, styleForStatus(thread.status));
  }
}

}  // namespace

Rect screenRect() {
  return {0, 0, 960, 540};
}

Rect contentRegion() {
  return {0, 114, 960, 426};
}

RenderStyle styleForStatus(CodeletStatus status) {
  return {
      usesHatchedBackground(status),
      usesDashedBorder(status),
      usesStrongBorder(status),
  };
}

void renderHome(DrawSink &sink, const Layout &layout, const CodeletSnapshot &snapshot, RenderMode mode,
                DevicePowerStatus power) {
  prepareFrame(sink, mode);
  renderQuota(sink, layout, snapshot, mode);
  if (!isFull(mode)) {
    sink.commit(layout.quota);
  }
  int layoutCardCount = layout.homeCardCount;
  if (layoutCardCount < 0) layoutCardCount = 0;
  if (layoutCardCount > 6) layoutCardCount = 6;
  size_t visibleCount = minSize(snapshot.visibleProjectCount, CODELET_MAX_PROJECTS);
  visibleCount = minSize(visibleCount, static_cast<size_t>(layoutCardCount));
  for (int i = 0; i < layoutCardCount; i++) {
    const ProjectView *project = static_cast<size_t>(i) < visibleCount ? &snapshot.projects[i] : nullptr;
    renderHomeCard(sink, layout.homeCards[i], project, mode);
  }
  renderTopChrome(sink, layout, snapshot, "Projects", mode, power);
  if (isFull(mode)) {
    sink.commit(screenRect());
  } else {
    if (layoutCardCount > 0) {
      sink.commit(homeCardsUnion(layout, layoutCardCount));
    }
    sink.commit(layout.header);
  }
}

void renderThreads(DrawSink &sink, const Layout &layout, const CodeletSnapshot &snapshot, RenderMode mode,
                   DevicePowerStatus power) {
  prepareFrame(sink, mode);
  renderQuota(sink, layout, snapshot, mode);
  if (!isFull(mode)) {
    sink.commit(layout.quota);
  }
  sink.fillRect(layout.threadsTable, GrayPaper);
  sink.drawRect(layout.threadsTable, {false, false, false});
  renderThreadsHeader(sink, layout.threadsTable);
  Rect body = threadsBody(layout.threadsTable);
  size_t visibleCount = minSize(snapshot.visibleThreadCount, CODELET_MAX_THREADS);
  visibleCount = minSize(visibleCount, static_cast<size_t>(maxRowsFor(body, 56, 56)));
  for (size_t i = 0; i < visibleCount; i++) {
    renderThreadTableRow(sink, rowRect(body, static_cast<int>(i), 56, 56), snapshot.threads[i]);
  }
  renderTopChrome(sink, layout, snapshot, "Threads", mode, power);
  if (isFull(mode)) {
    sink.commit(screenRect());
  } else {
    sink.commit(layout.header);
    sink.commit(layout.threadsTable);
  }
}

void renderProjectDetail(DrawSink &sink, const Layout &layout, const CodeletSnapshot &snapshot, const char *projectId,
                         RenderMode mode, DevicePowerStatus power) {
  prepareFrame(sink, mode);
  renderQuota(sink, layout, snapshot, mode);
  if (!isFull(mode)) {
    sink.commit(layout.quota);
  }
  sink.fillRect(layout.projectThreadList, GrayPaper);
  sink.drawRect(layout.projectThreadList, {false, false, false});

  const ProjectView *selected = nullptr;
  if (projectId != nullptr) {
    size_t projectCount = minSize(snapshot.visibleProjectCount, CODELET_MAX_PROJECTS);
    for (size_t i = 0; i < projectCount; i++) {
      if (stringsEqual(snapshot.projects[i].id, projectId)) {
        selected = &snapshot.projects[i];
        break;
      }
    }
  }

  if (selected != nullptr) {
    drawClippedText(sink, insetRect(layout.projectThreadList, 24, 18, 180, 38), selected->alias,
                    TextRole::ProjectTitle);
    drawClippedText(sink, {static_cast<int16_t>(layout.projectThreadList.x + layout.projectThreadList.w - 126),
                           static_cast<int16_t>(layout.projectThreadList.y + 22), 104, 30},
                    statusLabel(selected->state), TextRole::PrimaryText);
    char counts[96] = "";
    appendCount(counts, sizeof(counts), selected->threadCount, "threads");
    appendCount(counts, sizeof(counts), selected->counts.awaitingUser, "awaiting");
    appendCount(counts, sizeof(counts), selected->counts.failed, "failed");
    appendCount(counts, sizeof(counts), selected->counts.running, "running");
    drawClippedText(sink, insetRect(layout.projectThreadList, 24, 58, 24, 24), counts, TextRole::MetaText);
  }

  int16_t listTop = static_cast<int16_t>(layout.projectThreadList.y + 94);
  drawHLine(sink, static_cast<int16_t>(layout.projectThreadList.x + 24), listTop,
            static_cast<int16_t>(layout.projectThreadList.w - 48), GrayBlack);

  Rect header = {static_cast<int16_t>(layout.projectThreadList.x + 24),
                 static_cast<int16_t>(listTop + 14),
                 static_cast<int16_t>(layout.projectThreadList.w - 48), 24};
  drawClippedText(sink, columnRect(header, 0, 104), "STATUS", TextRole::MetaText);
  drawClippedText(sink, columnRect(header, 120, 500), "TITLE", TextRole::MetaText);
  drawClippedText(sink, columnRect(header, 638, 58), "AGE", TextRole::MetaText);
  drawClippedText(sink, columnRect(header, 712, static_cast<int16_t>(header.w - 712)), "LAST", TextRole::MetaText);
  drawHLine(sink, header.x, static_cast<int16_t>(header.y + header.h + 6), header.w, GrayBlack);

  size_t visibleCount = minSize(snapshot.visibleThreadCount, CODELET_MAX_THREADS);
  Rect rows = {static_cast<int16_t>(layout.projectThreadList.x + 24),
               static_cast<int16_t>(layout.projectThreadList.y + 144),
               static_cast<int16_t>(layout.projectThreadList.w - 48),
               static_cast<int16_t>(layout.projectThreadList.h - 158)};
  int maxRows = maxRowsFor(rows, 56, 56);
  int rowIndex = 0;
  for (size_t i = 0; i < visibleCount && rowIndex < maxRows; i++) {
    const ThreadView &thread = snapshot.threads[i];
    if (projectId == nullptr || !stringsEqual(thread.projectId, projectId)) {
      continue;
    }
    Rect row = rowRect(rows, rowIndex, 56, 56);
    renderProjectDetailRow(sink, row, thread);
    rowIndex++;
  }

  renderTopChrome(sink, layout, snapshot, "Projects", mode, power);
  if (isFull(mode)) {
    sink.commit(screenRect());
  } else {
    sink.commit(layout.header);
    sink.commit(layout.projectThreadList);
  }
}

void renderStatusPage(DrawSink &sink, const Layout &layout, const char *state, const char *detail, const char *hint) {
  (void)layout;
  sink.clear();
  Rect panel = {120, 128, 720, 250};
  sink.fillRect(panel, GrayPanel);
  sink.drawRect(panel, {false, false, true});
  drawClippedText(sink, {150, 148, 300, 58}, "Codelet", TextRole::Brand);
  drawClippedText(sink, {150, 218, 320, 30}, state == nullptr ? "STARTING" : state, TextRole::PrimaryText);
  drawClippedText(sink, {150, 262, 520, 28}, detail == nullptr ? "" : detail, TextRole::MetaText);
  drawHLine(sink, 150, 314, 660, GrayMid);
  drawClippedText(sink, {150, 330, 640, 28}, hint == nullptr ? "" : hint, TextRole::MetaText);
  sink.commit(screenRect());
}
