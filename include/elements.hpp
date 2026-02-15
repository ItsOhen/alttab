#pragma once

#include "defines.hpp"
#include <hyprland/src/defines.hpp>
#include <hyprland/src/desktop/view/Window.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <src/debug/log/Logger.hpp>
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

  bool hovered = false;

  virtual bool onMouseMove(const Vector2D &mousePos) {
    CBox box = {pos, size};
    bool isOver = box.containsPoint(mousePos);

    if (isOver != hovered) {
      hovered = isOver;
      onHoverChanged();
    }
    return isOver;
  }

  virtual void onHoverChanged() {
    Log::logger->log(Log::TRACE, "[{}] Element::onHoverChanged", PLUGIN_NAME);
  };

  virtual bool onMouseClick(const Vector2D &mousePos) {
    Vector2D absolutePos = pos;
    Element *p = parent;

    while (p) {
      absolutePos = absolutePos + p->pos;
      p = p->parent;
    }
    CBox box = {absolutePos.x, absolutePos.y, size.x, size.y};
    return box.containsPoint(mousePos);
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

  bool onMouseMove(const Vector2D &mousePos) override {
    bool anyChildHovered = false;
    for (auto &el : elements) {
      if (el->onMouseMove(mousePos)) {
        anyChildHovered = true;
      }
    }
    return anyChildHovered || Element::onMouseMove(mousePos);
  }

  bool onMouseClick(const Vector2D &mousePos) override {
    for (auto it = elements.rbegin(); it != elements.rend(); ++it) {
      if ((*it)->onMouseClick(mousePos)) {
        return true;
      }
    }
    return Element::onMouseClick(mousePos);
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

class Button : public Element {
public:
  CHyprColor color;
  std::function<void()> onClick;

  Button(CHyprColor col, std::function<void()> callback)
      : color(col), onClick(callback) {}

  void update(const double delta) override { alpha.tick(delta, ANIMATIONSPEED); }

  void draw(const Vector2D &offset) override {
    Log::logger->log(Log::TRACE, "[{}] Button::draw", PLUGIN_NAME);
    Vector2D renderPos = pos + offset;
    float drawAlpha = alphaAbs();

    CHyprColor drawCol = hovered ? color : color;

    if (size.x <= 1 || size.y <= 1) {
      Log::logger->log(Log::ERR, "[{}] Button::draw, invalid size: {}", PLUGIN_NAME, size);
      return;
    }
    CBox renderBox = {renderPos, size * scale.current};
    g_pHyprOpenGL->renderRect(renderBox, drawCol, {.round = 2});
  }

  bool onMouseClick(const Vector2D &mousePos) override {
    if (Element::onMouseClick(mousePos) && onClick) {
      onClick();
      return true;
    }
    return false;
  }

  void onHoverChanged() override {
    if (hovered) {
      scale.target = 1.1f;
      alpha.target = 1.0f;
    } else {
      scale.target = 1.0f;
      alpha.target = 0.8f;
    }
  }
};

class WindowContainer : public Container {
public:
  PHLWINDOW window;
  TextBox *header = nullptr;
  WindowSnapshot *snapshot = nullptr;
  BorderBox *border = nullptr;
  Button *closeButton = nullptr;

  void updateAnimation(float delta) {
  }

  void setTargetLayout(Vector2D p, Vector2D s, bool snap = false) {
    animPos.set(p, snap);
    animSize.set(s, snap);
  }

  WindowContainer(PHLWINDOW window);

  void draw(const Vector2D &offset) override;
  void update(double delta) override;
  bool onMouseClick(const Vector2D &mousePos) override;
};
