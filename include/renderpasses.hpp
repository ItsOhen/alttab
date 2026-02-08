#pragma once

#include "defines.hpp"
#include "elements.hpp"
#include <hyprland/src/helpers/Monitor.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/render/pass/PassElement.hpp>

class RenderPass : public IPassElement {
public:
  std::vector<Element *> elements;
  RenderPass(std::vector<Element *> e) : elements(e) {}

  virtual void draw(const CRegion &damage) {
    for (auto *el : elements) {
      el->draw({0, 0});
    }
  }

  virtual bool needsLiveBlur() { return false; }
  virtual bool needsPrecomputeBlur() { return false; }
  virtual const char *passName() { return "TabCarouselPassElement"; }

  virtual std::optional<CBox> boundingBox() {
    if (elements.empty())
      return std::nullopt;
    return CBox(MONITOR->m_position.x, MONITOR->m_position.y, MONITOR->m_size.x, MONITOR->m_size.y);
  }

  virtual CRegion opaqueRegion() { return {}; }
};

class BlurPass : public IPassElement {
public:
  virtual void draw(const CRegion &damage) {
    CBox monitorBox = {MONITOR->m_position.x, MONITOR->m_position.y, MONITOR->m_size.x, MONITOR->m_size.y};
    auto renderdata = CHyprOpenGLImpl::SRectRenderData{
        .damage = &damage,
        .blur = (*BLURENABLED ? true : false),
    };
    g_pHyprOpenGL->renderRect(monitorBox, DIMCOLOR, renderdata);
  }

  virtual bool needsLiveBlur() { return true; }
  virtual bool needsPrecomputeBlur() { return true; }
  virtual const char *passName() { return "TabCarouselBlurElement"; }

  virtual std::optional<CBox> boundingBox() {
    return CBox{MONITOR->m_position.x, MONITOR->m_position.y, MONITOR->m_size.x, MONITOR->m_size.y};
  }

  virtual CRegion opaqueRegion() { return {}; }
};
