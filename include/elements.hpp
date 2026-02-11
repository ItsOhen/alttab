#pragma once

#include "defines.hpp"
#include <hyprland/src/defines.hpp>
#include <hyprland/src/desktop/view/Window.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <src/desktop/state/FocusState.hpp>

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
};

class Element {
public:
  virtual ~Element() = default;
  void setParent(Element *parent) {
    this->parent = parent;
  };
  virtual void update(const double delta) {
    tick(delta, ANIMATIONSPEED);
  };
  virtual void tick(const double delta, float speed) {
    animPos.tick(delta, ANIMATIONSPEED);
    animSize.tick(delta, ANIMATIONSPEED);
    alpha.tick(delta, ANIMATIONSPEED);
    scale.tick(delta, ANIMATIONSPEED);
    this->pos = animPos.current;
    this->size = animSize.current;
  }
  virtual void draw(const Vector2D &offset) = 0;

  void markForRemoval() {
    markedForRemoval = true;
  };

  bool shouldBeRemoved() const {
    return markedForRemoval;
  };

  float alphaAbs() {
    if (!parent)
      return alpha.current;
    return alpha.current * parent->alphaAbs();
  }

  Element *parent = nullptr;
  bool markedForRemoval = false;
  Vector2D pos;
  Vector2D size;
  AnimatedValue<float> alpha{1.0f, 1.0f, 1.0f, 1.0f};
  AnimatedValue<float> scale{1.0f, 1.0f, 1.0f, 1.0f};
  AnimatedValue<Vector2D> animPos;
  AnimatedValue<Vector2D> animSize;
};

class Container : public Element {
public:
  std::vector<UP<Element>> elements;

  template <typename T, typename... Args>
  T *add(Args &&...args) {
    auto el = makeUnique<T>(std::forward<Args>(args)...);
    el->setParent(this);
    auto ptr = el.get();
    elements.emplace_back(std::move(el));
    return ptr;
  }

  void update(const double delta) override {
    Element::update(delta);
    cleanup();
    for (auto &el : elements)
      el->update(delta);
  }

  void draw(const Vector2D &offset) override {
    Log::logger->log(Log::TRACE, "Container::draw");
    Vector2D position = pos + offset;
    for (auto &el : elements)
      el->draw(position);
  }

  void cleanup() {
    std::erase_if(elements, [](auto &el) {
      if (auto sub = dc<Container *>(el.get())) {
        sub->cleanup();
      }
      return el->shouldBeRemoved();
    });
  }
};

class WindowSnapshot : public Element {
public:
  WindowSnapshot(PHLWINDOW window) : window(window) {};
  PHLWINDOW window;
  CFramebuffer fb;
  double borderSize = 1.0f;
  bool ready = false;
  std::chrono::time_point<std::chrono::steady_clock> lastUpdated;
  AnimatedValue<float> textureAlpha{0.0f, 0.0f, 0.0f, 0.0f};
  void draw(const Vector2D &offset) override;
  void update(const double delta) override;
  void snapshot();
};

class TextBox : public Element {
public:
  TextBox(PHLWINDOW w, CHyprColor color, int size) : window(w), color(color), fontsize(size) {};
  std::string text, lasttitle;
  PHLWINDOW window;
  CHyprColor color;
  SP<CTexture> texture;
  int fontsize;
  void update(const double delta) override;
  void draw(const Vector2D &offset) override;
};

class BorderBox : public Element {
public:
  BorderBox(PHLWINDOW w, int bs = 1, int r = 0, float power = 2) : window(w), bordersize(bs), rounding(r), power(power) {};
  PHLWINDOW window;
  int bordersize, rounding;
  float power;
  bool isActive = false;

  void update(const double delta) override;
  void draw(const Vector2D &offset) override;
};

class WindowContainer : public Container {
public:
  PHLWINDOW window;
  TextBox *header = nullptr;
  WindowSnapshot *snapshot = nullptr;
  BorderBox *border = nullptr;

  void updateAnimation(float delta) {
  }

  void setTargetLayout(Vector2D p, Vector2D s, bool snap = false) {
    animPos.set(p, snap);
    animSize.set(s, snap);
  }

  WindowContainer(PHLWINDOW window);

  void draw(const Vector2D &offset) override;
  void update(double delta) override;
};
