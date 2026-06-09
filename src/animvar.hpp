#pragma once
#include <algorithm>
#include <hyprlang.hpp>
#include <src/config/values/ConfigValues.hpp>
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
  SP<Config::Values::IValue> *speed = nullptr;

  AnimatedValue(SP<Config::Values::IValue> *speed) : speed(speed) {}

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
    auto casted = Hyprutils::Memory::dynamicPointerCast<Config::Values::CFloatValue>(*speed);
    float speedVal = casted ? casted->value() : 0.4f;
    progress = std::min(1.0f, progress + (delta / std::max(0.01f, speedVal)));
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
