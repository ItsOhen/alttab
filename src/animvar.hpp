#pragma once
#include <algorithm>

template <typename T>
struct AnimatedValue {
  T current{};
  T start{};
  T target{};
  float progress = 1.0f;

  AnimatedValue &operator=(const T &val) {
    set(val, false);
    return *this;
  }

  void snap(T val) {
    set(val, true);
  }

  void set(T val, bool snap) {
    if (snap) {
      current = start = target = val;
      progress = 1.0f;
    } else if (val != target) {
      start = current;
      target = val;
      progress = 0.0f;
    }
  }

  void tick(float delta, float speed) {
    if (progress >= 1.0f) {
      current = target;
      return;
    }
    progress = std::min(1.0f, progress + (delta / speed));
    float t = progress * (2.0f - progress);
    current = start + (target - start) * t;
  }

  bool done() {
    return progress >= 1.0f;
  }
};
