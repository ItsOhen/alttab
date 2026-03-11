#pragma once
#include "animvar.hpp"
#include "container.hpp"
#include "styles.hpp"
#include <src/desktop/state/FocusState.hpp>
#include <src/desktop/view/Window.hpp>
#include <src/render/Renderer.hpp>
#include <src/render/pass/TexPassElement.hpp>

class Monitor {
private:
  struct RenderTask {
    WindowCard *card;
    RenderData data;
    float visibility = 0.0f;
  };

protected:
  std::vector<RenderTask> renderTasks;

public:
  Monitor(PHLMONITOR monitor);
  void createTexture();
  WP<WindowCard> addWindow(PHLWINDOW window);
  size_t removeWindow(PHLWINDOW window);
  void update(const float delta, const Vector2D &offset, CRegion &damage);
  void draw(const CRegion &damage, const float alpha);
  void activeChanged();
  bool isActive() const;

  CBox position;
  AnimatedValue<float> rotation;
  AnimatedValue<float> zoom;
  AnimatedValue<float> alpha;
  PHLMONITOR monitor;
  SP<ITexture> texture;
  SP<ITexture> blurred;
  SP<IFramebuffer> bgFb, blurFb;
  size_t activeWindow = 0;
  std::vector<UP<WindowCard>> windows;

  friend class Manager;
};
