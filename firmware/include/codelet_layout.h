#pragma once

#include <stdint.h>

struct Rect {
  int16_t x;
  int16_t y;
  int16_t w;
  int16_t h;
};

struct Layout {
  Rect header;
  Rect quota;
  Rect navProjects;
  Rect navThreads;
  Rect homeCards[6];
  int homeCardCount;
  Rect threadsTable;
  Rect projectThreadList;
  Rect footer;
};

Layout buildLayout();
bool rectsOverlap(Rect a, Rect b);
