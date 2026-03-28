#pragma once

#include "defines.hpp"

struct StyleContext {
  size_t count;
  size_t active;
  float invCount;
  float angleStep;
  Vector2D mSize;
  Vector2D midpoint;
  float radius;
  float tiltOffset;
  float rotation, scale, alpha;
};

struct RenderData {
  bool visible;
  float z;
  float rotation;
  float scale;
  float alpha;
  CBox position;
};

struct MoveResult {
  bool changeMonitor = false;
  std::optional<size_t> index = std::nullopt;
};

class IStyle {
public:
  virtual ~IStyle() = default;
  virtual RenderData calculate(const StyleContext &ctx, const Vector2D &surfaceSize, const size_t index) const = 0;
  virtual MoveResult onMove(Direction dir, const size_t index, const size_t count) = 0;
};

class Carousel : public IStyle {
public:
  RenderData calculate(const StyleContext &ctx, const Vector2D &surfaceSize, const size_t index) const override;
  MoveResult onMove(Direction dir, const size_t index, const size_t count) override;
};

class Grid : public IStyle {
public:
  RenderData calculate(const StyleContext &ctx, const Vector2D &surfaceSize, const size_t index) const override;
  MoveResult onMove(Direction dir, const size_t index, const size_t count) override;

private:
  const int columns = 4;
};

class Slide : public IStyle {
public:
  RenderData calculate(const StyleContext &ctx, const Vector2D &surfaceSize, const size_t index) const override;
  MoveResult onMove(Direction dir, const size_t index, const size_t count) override;
};
