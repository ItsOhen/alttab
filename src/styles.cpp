#include "styles.hpp"
#include "defines.hpp"
#include <hyprutils/math/Vector2D.hpp>
#include <src/desktop/state/FocusState.hpp>
#include <src/helpers/Monitor.hpp>

RenderData Carousel::calculate(const StyleContext &ctx, const Vector2D &surfaceSize) const {
  const Vector2D center = {ctx.mSize.x / 2.0f, (ctx.mSize.y / 2.0f) + ctx.offset.y};

  const float baseAngle = ctx.rotation - ((2.0f * M_PI * ctx.index) / ctx.count);
  const float warpScale = Config::warp + (1.0f - Config::windowSizeInactive) * 0.2f;
  const float angle = baseAngle - warpScale * std::sin(2.0f * baseAngle);

  const float dist = std::abs(std::remainder(angle - (M_PI / 2.0f), 2.0f * M_PI));
  const float z = std::sin(angle);

  const float focusWeight = std::pow(std::max(0.0f, 1.0f - (float)(dist / (M_PI / 2.0f))), 2.5f);
  const float depthScale = std::lerp(Config::windowSizeInactive, 1.0f, (z + 1.0f) / 2.0f);
  const float scale = depthScale * std::lerp(1.0f, Config::windowSizeActive, focusWeight * ctx.scale);

  const float aspect = (surfaceSize.y > 0) ? surfaceSize.x / surfaceSize.y : 1.77f;
  Vector2D size = {ctx.mSize.y * Config::windowSize * aspect * scale, ctx.mSize.y * Config::windowSize * scale};

  if (size.x > ctx.mSize.x * Config::windowSize * 1.5f) {
    size.x = ctx.mSize.x * Config::windowSize * 1.5f;
    size.y = size.x / aspect;
  }

  const float radius = (ctx.mSize.x * 0.5f) * Config::carouselSize;
  const float radiusScale = radius * std::lerp(0.85f, 1.0f, (z + 1.0f) / 2.0f);
  const float tiltOffset = radius * std::sin(Config::tilt * (M_PI / 180.0f));

  const Vector2D pos = {
      center.x + (radiusScale * 1.4f) * std::cos(angle) - (size.x / 2.0f),
      (center.y - tiltOffset) - (z * -tiltOffset) - (size.y / 2.0f)};

  const float alphaWeight = std::pow(std::max(0.0f, 1.0f - (float)(dist / (M_PI / 1.25f))), 2.0f);
  const float baseline = std::lerp(0.0f, Config::unfocusedAlpha, ctx.alpha);
  const float finalAlpha = std::lerp(baseline + (z + 1.0f) * 0.2f, 1.0f, alphaWeight) * std::lerp(0.5f, 1.0f, ctx.alpha);

  const CBox box{pos, size};
  const bool isVisible = finalAlpha > 0.01f && box.overlaps({0, 0, ctx.mSize.x, ctx.mSize.y});

  return {
      .visible = isVisible,
      .z = z + (alphaWeight * 0.1f),
      .rotation = angle,
      .scale = scale,
      .alpha = std::clamp(finalAlpha, 0.0f, 1.0f),
      .position = box};
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

RenderData Grid::calculate(const StyleContext &ctx, const Vector2D &surfaceSize) const {
  const int rows = (ctx.count + cols - 1) / cols;
  const int curRow = ctx.index / cols;
  const int curCol = ctx.index % cols;
  const float gridWidth = ctx.mSize.x * Config::carouselSize;
  const Vector2D slot{gridWidth / cols, ctx.mSize.y * Config::carouselSize};
  const float baseScale = std::min(ctx.mSize.x, ctx.mSize.y) * Config::windowSize;
  const float focusWeight = (ctx.index == ctx.activeIndex) ? 1.0f : 0.0f;
  const float scale = Config::windowSizeInactive * std::lerp(1.0f, Config::windowSizeActive, focusWeight * ctx.scale);
  const float aspect = (surfaceSize.y > 0) ? surfaceSize.x / surfaceSize.y : 1.77f;
  Vector2D size = {baseScale * aspect * scale, baseScale * scale};
  const float gridStart = (ctx.mSize.x - gridWidth) / 2.0f;

  const Vector2D cCenter = {gridStart + (slot.x * curCol) + (slot.x / 2), (slot.y * curRow) + (slot.y / 2) + ctx.offset.y};
  const Vector2D pos = cCenter - (size / 2.0f);

  const float finalAlpha = std::lerp(Config::unfocusedAlpha, 1.0f, focusWeight) * ctx.alpha;
  const CBox box{pos, size};

  return {
      .visible = finalAlpha > 0.01f,
      .z = focusWeight, // Focused window stays on top
      .rotation = 0.0f,
      .scale = scale,
      .alpha = std::clamp(finalAlpha, 0.0f, 1.0f),
      .position = box};
}

MoveResult Grid::onMove(Direction dir, const size_t index, const size_t count) {
  if (count == 0)
    return {.changeMonitor = true};

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

RenderData Slide::calculate(const StyleContext &ctx, const Vector2D &surfaceSize) const {
  const float aspect = (surfaceSize.y > 0) ? surfaceSize.x / surfaceSize.y : 1.77f;
  const float activeH = ctx.mSize.y * Config::windowSizeActive * Config::windowSize;
  const float inactiveH = activeH * Config::windowSizeInactive;
  const float stripLoc = (ctx.rotation - (M_PI / 2.0f)) / (2.0f * M_PI) * ctx.count;
  const float dist = std::abs((float)ctx.index - stripLoc);
  const float focusWeight = std::pow(std::max(0.0f, 1.0f - dist), 2.5f);
  const float scale = std::lerp(1.0f, Config::windowSizeActive, focusWeight * ctx.scale);
  const float h = inactiveH * scale;
  const Vector2D size = {h * aspect, h};

  float stripIndex = (ctx.rotation - (M_PI / 2.0f)) / (2.0f * M_PI) * ctx.count;

  const float spacing = 1.2f;
  const float slotWidth = (inactiveH * aspect) * spacing;
  float xOffset = ((float)ctx.index - stripIndex) * slotWidth;

  const Vector2D center = {ctx.mSize.x / 2.0f, (ctx.mSize.y / 2.0f) + ctx.offset.y};
  const Vector2D pos = {
      center.x + xOffset - (size.x / 2.0f),
      center.y - (size.y / 2.0f)};

  const float finalAlpha = std::lerp(Config::unfocusedAlpha, 1.0f, focusWeight) * ctx.alpha;
  const CBox box{pos, size};

  return {
      .visible = finalAlpha > 0.01f && box.overlaps({0, 0, ctx.mSize.x, ctx.mSize.y}),
      .z = focusWeight,
      .rotation = 0.0f,
      .scale = h / activeH,
      .alpha = std::clamp(finalAlpha, 0.0f, 1.0f),
      .position = box};
}

MoveResult Slide::onMove(Direction dir, const size_t index, const size_t count) {
  if (dir == Direction::UP || dir == Direction::DOWN)
    return {.changeMonitor = true};

  int step = (dir == Direction::LEFT) ? -1 : 1;
  int target = index + step;

  if (target < 0 || target >= count)
    return {.index = index};

  return {.index = target};
}
