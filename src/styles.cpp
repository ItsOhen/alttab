#include "styles.hpp"
#include "defines.hpp"
#include "logger.hpp"
#include <hyprutils/math/Vector2D.hpp>
#include <src/desktop/state/FocusState.hpp>
#include <src/helpers/Monitor.hpp>
#include <xmmintrin.h>

RenderData Carousel::calculate(const StyleContext &ctx, const Vector2D &surfaceSize, const size_t index) const {
  const float angle = ctx.rotation - (ctx.angleStep * index);

  float s, c;
  sincosf(angle, &s, &c);

  const float z = s;
  const float zNorm = (z + 1.0f) * 0.5f;

  const float alphaWeight = (zNorm * 0.8f) + 0.2f;
  const float finalAlpha = alphaWeight * ctx.alpha;
  if (finalAlpha < 0.01f)
    return {.visible = false};

  const float wSize = Config::CWSize.value_or(Config::windowSize);
  const float activeSize = Config::CWSizeActive.value_or(Config::windowSizeActive);
  const float inactiveSize = Config::CWSizeInactive.value_or(Config::windowSizeInactive);

  float scale = inactiveSize + (1.0f - inactiveSize) * zNorm;
  if (z > 0.5f) {
    scale *= 1.0f + (activeSize - 1.0f) * ((z - 0.5f) * 2.0f * ctx.scale);
  }

  const float safeY = (surfaceSize.y > 1.0f) ? surfaceSize.y : 1.0f;
  const float aspect = surfaceSize.x / safeY;
  const float baseH = ctx.mSize.y * wSize * scale;
  const Vector2D size = {baseH * aspect, baseH};

  const Vector2D pos = {
      (ctx.midpoint.x + (ctx.radius * 1.4f * c)) - (size.x * 0.5f),
      (ctx.midpoint.y - ctx.tiltOffset) + (z * ctx.tiltOffset) - (size.y * 0.5f)};

  return {
      .visible = true,
      .z = z,
      .rotation = angle,
      .scale = scale,
      .alpha = (finalAlpha > 1.0f) ? 1.0f : finalAlpha,
      .position = {pos, size}};
}

MoveResult Carousel::onMove(Direction dir, const size_t index, const size_t count) {
  if (dir == Direction::UP || dir == Direction::DOWN)
    return {.changeMonitor = true};

  if (count == 0)
    return {.index = 0};

  if (dir == Direction::LEFT) {
    size_t nextIndex = (index == 0) ? count - 1 : index - 1;
    return {.index = nextIndex};
  } else {
    return {.index = (index + 1) % count};
  }
}

RenderData Grid::calculate(const StyleContext &ctx, const Vector2D &surfaceSize, const size_t index) const {
  const int cols = (int)Config::gridColumns.value_or((float)columns);
  const int rows = (ctx.count + cols - 1) / cols;
  const float spacing = Config::gridSpacing.value_or(0.0f) * ctx.scale;
  const float topPadding = spacing > 0 ? spacing : ctx.mSize.y * 0.1f * ctx.scale;

  const float gridW = ctx.mSize.x * Config::gridSize.value_or(0.8f);

  const float slotW = (gridW - (spacing * (cols + 1))) / cols;
  const float slotH = ctx.mSize.y * Config::GWSize.value_or(Config::windowSize);

  const int activeRow = ctx.active / cols;
  const float activeY = topPadding + (activeRow * (slotH + spacing)) + (slotH / 2.0f);
  const float visibleH = ctx.mSize.y;

  float scrollOffset = 0.0f;
  const float rowTop = activeRow * (slotH + spacing);
  const float rowBottom = rowTop + slotH;

  if (activeRow > 0 && rowTop < scrollOffset) {
    scrollOffset = rowTop;
  } else if (rowBottom > visibleH) {
    scrollOffset = rowBottom - visibleH;
  }

  const int curRow = (int)(index / cols);
  const int curCol = (int)(index % cols);

  const float isActive = index == ctx.active ? 1.0f : 0.0f;
  const float windowScale = isActive ? Config::GWSizeActive.value_or(Config::windowSizeActive) : Config::GWSizeInactive.value_or(Config::windowSizeInactive);
  const float finalScale = windowScale * ctx.scale;

  const float winW = slotW * finalScale;
  const float winH = slotH * finalScale;
  Vector2D size = {winW, winH};

  const float gridStartX = (ctx.mSize.x - gridW) / 2.0f;

  const float x = gridStartX + spacing + (curCol * (slotW + spacing)) + (slotW / 2.0f);
  const float y = topPadding + ((curRow * (slotH + spacing)) - scrollOffset) + (slotH / 2.0f);

  const Vector2D pos = {x - (size.x / 2.0f), y - (size.y / 2.0f)};

  const float finalAlpha = std::lerp(Config::unfocusedAlpha, 1.0f, isActive) * ctx.alpha;
  const CBox box{pos, size};

  return {
      .visible = finalAlpha > 0.01f,
      .z = isActive,
      .rotation = 0.0f,
      .scale = finalScale,
      .alpha = std::clamp(finalAlpha, 0.0f, 1.0f),
      .position = box};
}

MoveResult Grid::onMove(Direction dir, const size_t index, const size_t count) {
  if (count == 0)
    return {.changeMonitor = true};

  const int cols = (int)Config::gridColumns.value_or((float)columns);
  const int rows = (count + cols - 1) / cols;
  int curRow = index / cols;
  int curCol = index % cols;

  switch (dir) {
  case Direction::UP:
    if (--curRow < 0)
      return {.changeMonitor = true};
    break;
  case Direction::DOWN:
    if (++curRow >= rows)
      return {.changeMonitor = true};
    break;
  case Direction::LEFT:
    if (--curCol < 0)
      curCol = cols - 1;
    break;
  case Direction::RIGHT:
    if (++curCol >= cols)
      curCol = 0;
    break;
  }

  int target = curRow * cols + curCol;

  if (target >= count)
    target = count - 1;

  if ((size_t)target == index && (dir == Direction::UP || dir == Direction::DOWN))
    return {.changeMonitor = true};

  return {.index = (size_t)target};
}

RenderData Slide::calculate(const StyleContext &ctx, const Vector2D &surfaceSize, const size_t index) const {
  const float aspect = (surfaceSize.y > 0) ? surfaceSize.x / surfaceSize.y : 1.77f;
  const float activeH = ctx.mSize.y * Config::slideSizeActive.value_or(Config::windowSizeActive) * Config::windowSize;
  const float inactiveH = activeH * Config::slideSizeInactive.value_or(1.0f);

  float stripIndex = (ctx.rotation - (M_PI / 2.0f)) / (2.0f * M_PI) * ctx.count;

  float visualSlot = (float)index;
  if (index > ctx.count / 2) {
    visualSlot -= (float)ctx.count;
  }

  float diff = visualSlot - stripIndex;
  const float dist = std::abs(diff);
  const float focusWeight = std::pow(std::max(0.0f, 1.0f - dist), 2.5f);
  const float h = inactiveH * (1.0f + focusWeight * (Config::slideSizeActive.value_or(1.0f) - 1.0f));
  const float finalScale = (h / activeH) * ctx.scale;
  const Vector2D size = {h * aspect, h};

  const float spacing = Config::slideSize.value_or(50.0f) * ctx.scale;
  const float slotWidth = inactiveH * aspect + spacing;
  float xOffset = diff * slotWidth;

  const Vector2D center = {ctx.mSize.x / 2.0f, (ctx.mSize.y / 2.0f)};
  const Vector2D pos = {center.x + xOffset - (size.x / 2.0f), center.y - (size.y / 2.0f)};

  const float finalAlpha = std::lerp(Config::unfocusedAlpha, 1.0f, focusWeight) * ctx.alpha;
  const CBox box{pos, size};

  return {
      .visible = finalAlpha > 0.01f && box.overlaps({0, 0, ctx.mSize.x, ctx.mSize.y}),
      .z = focusWeight,
      .rotation = 0.0f,
      .scale = finalScale,
      .alpha = std::clamp(finalAlpha, 0.0f, 1.0f),
      .position = box};
}

MoveResult Slide::onMove(Direction dir, const size_t index, const size_t count) {
  if (dir == Direction::UP || dir == Direction::DOWN)
    return {.changeMonitor = true};

  if (count <= 1)
    return {.index = index};

  auto getSlot = [&](int idx) {
    if (idx == 0)
      return 0;
    if (idx > (int)count / 2)
      return idx - (int)count;
    return idx;
  };

  auto getIndexFromSlot = [&](int slot) {
    return slot < 0 ? slot + count : slot;
  };

  int currentSlot = getSlot(index);
  int step = (dir == Direction::LEFT) ? -1 : 1;
  int targetSlot = currentSlot + step;

  int minSlot = (count % 2 == 0) ? -(int)count / 2 + 1 : -(int)count / 2;
  int maxSlot = (int)count / 2;

  if (targetSlot < minSlot || targetSlot > maxSlot)
    return {.index = index};

  return {.index = (size_t)getIndexFromSlot(targetSlot)};
}
