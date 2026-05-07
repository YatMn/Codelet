#include "codelet_layout.h"

Layout buildLayout() {
  Layout layout{};
  layout.header = {24, 16, 614, 98};
  layout.quota = {656, 22, 280, 92};
  layout.navProjects = {398, 26, 128, 36};
  layout.navThreads = {532, 26, 106, 36};
  layout.homeCardCount = 4;

  const int margin = 24;
  const int gap = 16;
  const int top = 122;
  const int cardW = (960 - margin * 2 - gap) / 2;
  const int cardH = (540 - top - margin - gap) / 2;
  for (int row = 0; row < 2; row++) {
    for (int col = 0; col < 2; col++) {
      int index = row * 2 + col;
      layout.homeCards[index] = {
          static_cast<int16_t>(margin + col * (cardW + gap)),
          static_cast<int16_t>(top + row * (cardH + gap)),
          static_cast<int16_t>(cardW),
          static_cast<int16_t>(cardH),
      };
    }
  }

  layout.threadsTable = {24, 128, 912, 392};
  layout.projectThreadList = {24, 128, 912, 392};
  layout.footer = {0, 0, 0, 0};
  return layout;
}

bool rectsOverlap(Rect a, Rect b) {
  return a.x < b.x + b.w && a.x + a.w > b.x && a.y < b.y + b.h && a.y + a.h > b.y;
}
