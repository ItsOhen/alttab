#pragma once

#include <hyprland/src/defines.hpp>
#include <hyprland/src/desktop/view/Window.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <src/desktop/state/FocusState.hpp>

class Element {
public:
  virtual ~Element() = default;
  void setParent(Element *parent) {
    this->parent = parent;
  };
  virtual void update() = 0;
  virtual void draw(const Vector2D &offset) = 0;

  void markForRemoval() {
    markedForRemoval = true;
  };

  bool shouldBeRemoved() const {
    return markedForRemoval;
  };

protected:
  Element *parent = nullptr;
  bool markedForRemoval = false;
  Vector2D pos;
  Vector2D size;
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

  void update() override {
    cleanup();
    for (auto &el : elements)
      el->update();
  }

  void draw(const Vector2D &offset) override {
    Log::logger->log(Log::ERR, "Container::draw");
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
  void update();
  void draw(const Vector2D &offset);
  friend class WindowContainer;
};

class TextBox : public Element {
public:
  TextBox(PHLWINDOW w, CHyprColor color, int size) : window(w), color(color), fontsize(size) {};
  std::string text, lasttitle;
  PHLWINDOW window;
  CHyprColor color;
  SP<CTexture> texture;
  int fontsize;
  void update();
  void draw(const Vector2D &offset);
  friend class WindowContainer;
};

class BorderBox : public Element {
public:
  BorderBox(PHLWINDOW w) : window(w) {};
  PHLWINDOW window;
  CHyprColor activeColor = {1.0f, 0.5f, 0.0f, 1.0f};
  CHyprColor inactiveColor = {0.3f, 0.3f, 0.3f, 0.8f};
  bool isActive = false;

  void update() override { ; };
  void draw(const Vector2D &offset) override;
  friend class WindowContainer;
};

class WindowContainer : public Container {
public:
  PHLWINDOW window;
  TextBox *header = nullptr;
  WindowSnapshot *snapshot = nullptr;
  BorderBox *border = nullptr;
  Vector2D targetSize, targetPos;

  WindowContainer(PHLWINDOW window);

  void draw(const Vector2D &offset);

  void onResize();

  friend class CarouselManager;
};
