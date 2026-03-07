#pragma once
#include "container.hpp"
#include "defines.hpp"
#include "styles.hpp"

class Monitor {
private:
  struct RenderTask {
    WindowCard *card;
    RenderData data;
    float visibility = 0.0f;
    float since = 0.0f;
  };

protected:
  std::vector<RenderTask> renderTasks;

public:
  Monitor(PHLMONITOR monitor);
  void createTexture();
  void renderTexture(const CRegion &damage);
  WP<WindowCard> addWindow(PHLWINDOW window);
  size_t removeWindow(PHLWINDOW window);
  bool animate(const float delta);
  void update(const float delta, const Vector2D &offset);
  void draw(const CRegion &damage, const float alpha);
  void activeChanged();
  bool isActive() const;

  CBox position;
  bool animating = false;
  AnimatedValue<float> rotation;
  AnimatedValue<float> zoom;
  AnimatedValue<float> alpha;
  Timestamp lastFrame;
  PHLMONITOR monitor;
  SP<CTexture> texture;
  SP<CTexture> blurred;
  CFramebuffer bgFb, blurFb;
  size_t activeWindow = 0;
  std::vector<UP<WindowCard>> windows;

  friend class Manager;
};
