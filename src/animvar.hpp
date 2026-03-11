#pragma once
#include <algorithm>
#include <src/helpers/memory/Memory.hpp>
#include <vector>

class IAnimatedValue;
class AnimationManager {
public:
  void add(IAnimatedValue *val);
  void remove(IAnimatedValue *val);
  bool tick(const float delta);
  static AnimationManager &get() {
    static AnimationManager instance;
    return instance;
  }

private:
  std::vector<IAnimatedValue *> variables;
  bool animating = false;
};

class IAnimatedValue {
public:
  IAnimatedValue() {
    AnimationManager::get().add(this);
  };
  ~IAnimatedValue() {
    AnimationManager::get().remove(this);
  }
  virtual void tick(float delta) = 0;
  virtual bool done() = 0;
};

template <typename T>
struct AnimatedValue : public IAnimatedValue {
  T current{};
  T start{};
  T target{};
  float progress = 1.0f;
  float speed = 0.5f;

  AnimatedValue(const float speed) : speed(speed) {}

  AnimatedValue &operator=(const T &val) {
    set(val, false);
    return *this;
  }

  void snap(T val) {
    set(val, true);
  }

  void set(T val, bool snap = false) {
    if (snap) {
      current = start = target = val;
      progress = 1.0f;
    } else if (val != target) {
      start = current;
      target = val;
      progress = 0.0f;
    }
  }

  void tick(float delta) {
    if (progress >= 1.0f) {
      current = target;
      return;
    }
    progress = std::min(1.0f, progress + (delta / std::max(0.01f, speed)));
    float t = progress * (2.0f - progress);
    current = start + (target - start) * t;
  }

  bool done() {
    return progress >= 1.0f;
  }
};

inline void AnimationManager::add(IAnimatedValue *val) {
  if (std::find(variables.begin(), variables.end(), val) == variables.end()) {
    variables.emplace_back(val);
  }
}
inline void AnimationManager::remove(IAnimatedValue *val) {
  std::erase(variables, val);
};
inline bool AnimationManager::tick(const float delta) {
  animating = false;
  for (auto &var : variables) {
    var->tick(delta);
    animating |= !var->done();
  }
  return animating;
}
