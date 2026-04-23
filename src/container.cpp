#include "container.hpp"
#include "defines.hpp"
#include "helpers.hpp"
#include "logger.hpp"
#include <hyprutils/math/Vector2D.hpp>
#include <src/desktop/state/FocusState.hpp>
#include <src/desktop/view/Window.hpp>
#include <src/helpers/Color.hpp>
#include <src/protocols/PresentationTime.hpp>
#include <src/render/pass/BorderPassElement.hpp>
#include <src/render/pass/RectPassElement.hpp>
#include <src/render/pass/TexPassElement.hpp>
#define protected public
#include <src/render/OpenGL.hpp>
#include <src/render/Renderer.hpp>
#undef protected

WindowCard::WindowCard(PHLWINDOW window) : window(window) {
  ;
}

void WindowCard::setPosition(const CBox &position) {
  this->position = position;
}

CBox WindowCard::getPosition() const {
  return position;
}

void WindowCard::draw(const CRegion& damage) {
  LOG_SCOPE(Log::DRAW);

  if (!window || !window->wlSurface() || !window->wlSurface()->resource())
    return;

  const auto MONITOR = Desktop::focusState()->monitor();

  const float scale = MONITOR->m_scale;
  const float alpha = 1.0f;

  auto layout = buildLayout(scale);

  updateTitleTexture(scale);

  // Title bar background — deferred
  {
    CRectPassElement::SRectData rect;
    rect.box = layout.title;
    rect.color = {0, 0, 0, 0.8f * alpha};
    g_pHyprRenderer->m_renderPass.add(makeUnique<CRectPassElement>(rect));
  }

  // Preview background — deferred
  {
    CRectPassElement::SRectData rect;
    rect.box = layout.preview;
    rect.color = {0, 0, 0, alpha};
    g_pHyprRenderer->m_renderPass.add(makeUnique<CRectPassElement>(rect));
  }

  // Border — deferred
  {
    CBorderPassElement::SBorderData border;
    border.box = layout.outer;
    border.grad1 = (isActive) ? *Config::activeBorderColor : *Config::inactiveBorderColor;
    border.borderSize = Config::borderSize;
    border.round = Config::borderRounding;
    border.roundingPower = Config::borderRoundingPower;
    g_pHyprRenderer->m_renderPass.add(makeUnique<CBorderPassElement>(border));
  }

  // Window surface texture — deferred
  window->wlSurface()->resource()->breadthfirst(
      [&](SP<CWLSurfaceResource> s, const Vector2D &offset, void *) {
        if (!s->m_current.texture)
          return;

        CTexPassElement::SRenderData tex;
        tex.tex = s->m_current.texture;
        tex.box = {
            layout.preview.pos() + (offset * scale),
            layout.preview.size()};
        tex.a = alpha;

        g_pHyprRenderer->m_renderPass.add(makeUnique<CTexPassElement>(tex));
      },
      nullptr);

  // Title text — deferred
  if (titleTexture) {

    Vector2D size = titleTexture->m_size * scale;
    Vector2D pos = layout.title.pos() + (layout.title.size() - size) * 0.5f;

    CTexPassElement::SRenderData tex;
    tex.tex = titleTexture;
    tex.box = {pos, size};
    tex.a = alpha;

    g_pHyprRenderer->m_renderPass.add(makeUnique<CTexPassElement>(tex));
  }

#ifndef NDEBUG
  // Debug overlay — deferred
  {
    CRectPassElement::SRectData rect;
    rect.box = layout.outer;
    rect.color = {0.0, 0.0, 1.0, 0.1};
    g_pHyprRenderer->m_renderPass.add(makeUnique<CRectPassElement>(rect));
  }
#endif

  // NO per-card flush — the single flush happens in Manager::onRender()
}

void WindowCard::present() {
  const auto MONITOR = Desktop::focusState()->monitor();
  window->wlSurface()->resource()->breadthfirst(
      [&](SP<CWLSurfaceResource> s, const Vector2D &offset, void *) { s->presentFeedback(NOW, MONITOR, false); },
      nullptr);
}

CardLayout WindowCard::buildLayout(float scale) {
  CardLayout l;
  l.outer = position;
  l.outer.round();
  l.outer.scale(scale);

  l.content = l.outer.expand(-Config::borderSize);

  const float padding = 4;
  const float barHeight = (Config::fontSize + padding) * scale;

  l.title = {
      l.content.x,
      l.content.y,
      l.content.width,
      barHeight};

  l.preview = {
      l.content.x,
      l.content.y + barHeight,
      l.content.width,
      l.content.height - barHeight};

  return l;
}

void WindowCard::updateTitleTexture(float scale) {
  if (!window)
    return;

  float baseWidth = position.width;
  float padding = 10.f;

  if (window->m_title == title && std::abs(lastBaseWidth - baseWidth) < 1.f)
    return;

  lastBaseWidth = baseWidth;
  title = window->m_title;

  int maxChars = std::max(
      5.0f,
      (float)((baseWidth - padding) / (Config::fontSize * 0.55f)));
  auto display = middleTruncate(title, maxChars);
  titleTexture = g_pHyprOpenGL->renderText(display, CHyprColor(1, 1, 1, 1), Config::fontSize);
}
