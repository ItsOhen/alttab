#include "../include/elements.hpp"
#include "../include/defines.hpp"
#include "../include/helpers.hpp"
#define private public
#include <hyprland/src/render/Renderer.hpp>
#undef private

void WindowSnapshot::update() {
  Log::logger->log(Log::TRACE, "[{}] WindowSnapshot::update", PLUGIN_NAME);
  if (!window || !MONITOR || !window->wlSurface()) {
    Log::logger->log(Log::ERR, "[{}] WindowSnapshot::update, invalid window/monitor/surface", PLUGIN_NAME);
    return;
  }

  Vector2D surfaceSize = window->wlSurface()->getViewporterCorrectedSize();
  if (surfaceSize.x < 1.0 || surfaceSize.y < 1.0) {
    Log::logger->log(Log::ERR, "[{}] WindowSnapshot::update, invalid surface size: {}", PLUGIN_NAME, surfaceSize);
    return;
  }

  if (size.x <= 1 || size.y <= 1) {
    Log::logger->log(Log::ERR, "[{}] WindowSnapshot::update, invalid size: {}", PLUGIN_NAME, size);
    return;
  }

  auto rounded = size;
  rounded.round();
  if (rounded.x >= 1.0 && rounded.y >= 1.0) {
    if (rounded != fb.m_size) {
      fb.alloc(rounded.x, rounded.y, MONITOR->m_output->state->state().drmFormat);
    }
  }

  g_pHyprRenderer->makeEGLCurrent();

  CRegion fbBox = CBox{{0, 0}, rounded};
  if (!g_pHyprRenderer->beginRender(MONITOR, fbBox, RENDER_MODE_FULL_FAKE, nullptr, &fb)) {
    Log::logger->log(Log::ERR, "[{}] WindowSnapshot::update, beginRender failed", PLUGIN_NAME);
    return;
  }

  g_pHyprOpenGL->clear(CHyprColor{0, 0, 0, 1.0f});

  double scale = std::min(size.x / surfaceSize.x, size.y / surfaceSize.y);
  auto root = window->wlSurface()->resource();

  root->breadthfirst([&](SP<CWLSurfaceResource> s, const Vector2D &offset, void *) {
    if (!s->m_current.texture)
      return;

    auto box = s->extends();
    box.scale(scale).translate(offset * scale);
    g_pHyprOpenGL->renderTexture(s->m_current.texture, box, {.a = 1.0f});
  },
                     nullptr);

  g_pHyprRenderer->endRender();
}

void WindowSnapshot::draw(const Vector2D &offset) {
  Log::logger->log(Log::TRACE, "[{}] WindowSnapshot::draw", PLUGIN_NAME);
  auto tex = fb.getTexture();
  if (!tex) {
    Log::logger->log(Log::ERR, "[{}] WindowSnapshot::draw, texture is NULL", PLUGIN_NAME);
    return;
  }
  CBox box = {pos, size};
  box.translate(offset);
  g_pHyprOpenGL->renderTexture(tex, box, {});
}

void TextBox::update() {
  Log::logger->log(Log::TRACE, "[{}] TextBox::update", PLUGIN_NAME);
  if (size.x <= 1 || size.y <= 1) {
    return;
  }
  if (window->m_title != lasttitle) {
    lasttitle = window->m_title;
    // 0.6 might be correct?
    auto div = std::max(1.0, (**FONTSIZE * 0.6));
    text = middleTruncate(window->m_title, (size.x / div));
  }
}

void TextBox::draw(const Vector2D &offset) {
  Log::logger->log(Log::TRACE, "[{}] TextBox::draw", PLUGIN_NAME);
  if (!FONTSIZE) {
    Log::logger->log(Log::ERR, "[{}] TextBox::draw, no font size", PLUGIN_NAME);
    return;
  }
  if (!window) {
    Log::logger->log(Log::ERR, "[{}] TextBox::draw, no window", PLUGIN_NAME);
  }
  if (size.x <= 1 || size.y <= 1) {
    Log::logger->log(Log::ERR, "[{}] TextBox::draw, invalid size: {}", PLUGIN_NAME, size);
    return;
  }
  g_pHyprOpenGL->renderRect({offset, size}, CHyprColor{0, 0, 0, 0.2f}, {});
  if (!texture) {
    texture = g_pHyprOpenGL->renderText(text, color, **FONTSIZE);
  }
  if (!texture) {
    Log::logger->log(Log::ERR, "[{}] TextBox::draw, no texture", PLUGIN_NAME);
    return;
  }
  auto scale = size.y / texture->m_size.y;
  auto width = texture->m_size.x * scale;
  float center = (size.x - width) / 2.0f;
  if (center < 0)
    center = 0;
  auto box = CBox{offset.x + center, offset.y, width, size.y};
  g_pHyprOpenGL->renderTexture(texture, box, {});
}

void WindowContainer::onResize() {
  Log::logger->log(Log::TRACE, "[{}] WindowContainer::onResize", PLUGIN_NAME);
  if (!header || !snapshot) {
    Log::logger->log(Log::ERR, "[{}] WindowContainer::onResize, header? {}", PLUGIN_NAME, header ? "true" : "false");
    Log::logger->log(Log::ERR, "[{}] WindowContainer::onResize, snapshot? {}", PLUGIN_NAME, snapshot ? "true" : "false");
    return;
  }
  header->pos = Vector2D(0, 0);
  header->size = Vector2D(size.x, (double)header->fontsize);
  snapshot->pos = Vector2D(0.0, header->size.y);
  snapshot->size = Vector2D(size.x, size.y - header->size.y);
  border->pos = Vector2D(0, 0);
  border->size = Vector2D(size.x, size.y);
}

void WindowContainer::draw(const Vector2D &offset) {
  Log::logger->log(Log::TRACE, "[{}] WindowContainer::draw", PLUGIN_NAME);
  Container::draw(offset.round());
}

WindowContainer::WindowContainer(PHLWINDOW window) : window(window) {
  Log::logger->log(Log::TRACE, "[{}] WindowContainer::WindowContainer", PLUGIN_NAME);
  header = add<TextBox>(window, TITLECOLOR, **FONTSIZE);
  snapshot = add<WindowSnapshot>(window);
  border = add<BorderBox>(window);
  onResize();
};
void BorderBox::draw(const Vector2D &offset) {
  Log::logger->log(Log::TRACE, "[{}] BorderBox::draw", PLUGIN_NAME);
  if (size.x <= 1 || size.y <= 1) {
    Log::logger->log(Log::ERR, "[{}] BorderBox::draw, invalid size: {}", PLUGIN_NAME, size);
    return;
  }
  auto box = CBox{offset, size}.round();
  g_pHyprOpenGL->renderBorder(box, isActive ? activeColor : inactiveColor, {.round = 3, .borderSize = 2});
}
