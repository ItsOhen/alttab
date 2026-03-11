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
#include <src/render/pass/TexPassElement.hpp>
#define protected public
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

void WindowCard::draw() {
  LOG_SCOPE(Log::DRAW);

  if (!window)
    return;

  const auto MONITOR = Desktop::focusState()->monitor();

  const float scale = 1.0f;
  const float alpha = 1.0f;

  auto layout = buildLayout(scale);

  updateTitleTexture(scale);

  {
    CRectPassElement::SRectData rect;
    rect.box = layout.title;
    rect.color = {0, 0, 0, 0.8f * alpha};

    g_pHyprRenderer->m_renderPass.add(makeUnique<CRectPassElement>(rect));
  }

  {
    CRectPassElement::SRectData rect;
    rect.box = layout.preview;
    rect.color = {0, 0, 0, alpha};

    g_pHyprRenderer->m_renderPass.add(makeUnique<CRectPassElement>(rect));
  }

  {
    CBorderPassElement::SBorderData border;
    border.box = layout.outer;
    border.grad1 = (isActive) ? *Config::activeBorderColor : *Config::inactiveBorderColor;
    border.borderSize = Config::borderSize;
    border.round = Config::borderRounding;
    border.roundingPower = Config::borderRoundingPower;
    g_pHyprRenderer->m_renderPass.add(makeUnique<CBorderPassElement>(border));
  }

  window->wlSurface()->resource()->breadthfirst(
      [&](SP<CWLSurfaceResource> s, const Vector2D &offset, void *) {
        if (!s->m_current.texture)
          return;

        CTexPassElement::SRenderData tex;
        tex.tex = s->m_current.texture;
        tex.box = {
            layout.preview.pos() + offset,
            layout.preview.size()};
        tex.a = alpha;

        g_pHyprRenderer->m_renderPass.add(makeUnique<CTexPassElement>(tex));

        s->presentFeedback(NOW, MONITOR, true);
      },
      nullptr);

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
  CRectPassElement::SRectData debug;
  debug.box = position;
  debug.color = {1.0, 1.0, 0.0, 0.1};
  g_pHyprRenderer->m_renderPass.add(makeUnique<CRectPassElement>(debug));
#endif
}

CardLayout WindowCard::buildLayout(float scale) {
  CardLayout l;

  l.outer = position;
  l.outer.round();

  l.content = l.outer.expand(-Config::borderSize * scale);

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

  float baseWidth = position.width / scale;
  float padding = 10.f;

  if (window->m_title == title)
    return;

  title = window->m_title;

  int maxChars = std::max(
      5.0f,
      (float)((baseWidth - padding) / (Config::fontSize * 0.55f)));
  auto display = middleTruncate(title, maxChars);
  titleTexture = g_pHyprRenderer->renderText(display, CHyprColor(1, 1, 1, 1), Config::fontSize);
}
