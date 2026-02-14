#pragma once

#include "defines.hpp"
#include "elements.hpp"
#include <hyprland/src/helpers/Monitor.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/render/pass/PassElement.hpp>
#include <src/Compositor.hpp>
#include <src/render/OpenGL.hpp>

class RenderPass : public IPassElement {
public:
  std::vector<Element *> elements;
  RenderPass(std::vector<Element *> e) : elements(e) {}

  virtual void draw(const CRegion &damage) {
    auto mon = g_pHyprOpenGL->m_renderData.pMonitor.lock();
    if (!mon || !(mon == MONITOR))
      return;
    for (auto *el : elements) {
      el->draw({0, 0});
    }
  }

  virtual bool needsLiveBlur() { return false; }
  virtual bool needsPrecomputeBlur() { return false; }
  virtual const char *passName() { return "TabCarouselPassElement"; }
};

class BlurPass : public IPassElement {
public:
  virtual void draw(const CRegion &damage) {
    auto mon = g_pHyprOpenGL->m_renderData.pMonitor.lock();
    if (!mon)
      return;

    auto localBox = CBox{{0, 0}, mon->m_size}.scale(mon->m_scale);
    auto renderdata = CHyprOpenGLImpl::SRectRenderData{
        .damage = &damage,
        .blur = (*BLURENABLED ? true : false),
    };

    g_pHyprOpenGL->renderRect(localBox, DIMCOLOR, renderdata);
  }

  virtual bool needsLiveBlur() { return false; }
  virtual bool needsPrecomputeBlur() { return false; }
  virtual const char *passName() { return "TabCarouselBlurElement"; }
};
