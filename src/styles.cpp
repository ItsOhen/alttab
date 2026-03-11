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
#if defined(__linux__)
  sincosf(angle, &s, &c);
#else
  s = std::sin(angle);
  c = std::cos(angle);
#endif

  const float z = s;
  const float zNorm = (z + 1.0f) * 0.5f;

  const float alphaWeight = (zNorm * 0.8f) + 0.2f;
  const float finalAlpha = alphaWeight * ctx.alpha;
  if (finalAlpha < 0.01f)
    return {.visible = false};

  LOG(Log::STYLE, "CWSize {}, CWSizeActive {}, CWSizeInactive {}", Config::CWSize, Config::CWSizeActive, Config::CWSizeInactive);

  float scale = Config::CWSizeInactive + (1.0f - Config::CWSizeInactive) * zNorm;
  if (z > 0.5f) {
    scale *= 1.0f + (Config::CWSizeActive - 1.0f) * ((z - 0.5f) * 2.0f * ctx.scale);
  }

  const float safeY = (surfaceSize.y > 1.0f) ? surfaceSize.y : 1.0f;
  const float aspect = surfaceSize.x / safeY;
  const float baseH = ctx.mSize.y * ((Config::CWSize) ? Config::CWSize : Config::windowSize) * scale;
  const Vector2D size = {baseH * aspect, baseH};

  const Vector2D pos = {
      (ctx.midpoint.x + (ctx.radius * 1.4f * c)) - (size.x * 0.5f),
      (ctx.midpoint.y + ctx.offset.y - ctx.tiltOffset) + (z * ctx.tiltOffset) - (size.y * 0.5f)};

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
  return {.visible = false};
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

RenderData Slide::calculate(const StyleContext &ctx, const Vector2D &surfaceSize, const size_t index) const {
  return {.visible = false};
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
