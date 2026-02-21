#include "../include/elements.hpp"
#include "../include/defines.hpp"
#include "../include/helpers.hpp"
#include "../include/manager.hpp"
#include <src/Compositor.hpp>
#define private public
#include <hyprland/src/render/Renderer.hpp>
#undef private

void WindowSnapshot::update(const double delta) {
}

void WindowSnapshot::snapshot() {
  Log::logger->log(Log::TRACE, "[{}] WindowSnapshot::update", PLUGIN_NAME);
  if (!window || !MONITOR || !window->wlSurface()) {
    Log::logger->log(Log::ERR, "[{}] WindowSnapshot::update, invalid window/monitor/surface", PLUGIN_NAME);
    return;
  }

  auto targetSize = ((WindowContainer *)parent)->animSize.target.round();

  if (targetSize.x <= 1 || targetSize.y <= 1)
    return;

  auto surfaceSize = window->wlSurface()->getSurfaceBoxGlobal().value_or({0, 0, 0, 0}).size();
  if (surfaceSize.x < 1.0 || surfaceSize.y < 1.0) {
    Log::logger->log(Log::ERR, "[{}] WindowSnapshot::update, invalid surface size: {}", PLUGIN_NAME, surfaceSize);
    return;
  }

  if (targetSize.x > 0 && targetSize.y > 0) {
    if (targetSize > fb.m_size) {
      fb.alloc(targetSize.x, targetSize.y, MONITOR->m_output->state->state().drmFormat);
    }
  }

  g_pHyprRenderer->makeEGLCurrent();

  CRegion fbBox = CBox{{0, 0}, targetSize};
  if (!g_pHyprRenderer->beginRender(MONITOR, fbBox, RENDER_MODE_FULL_FAKE, nullptr, &fb)) {
    return;
  }

  g_pHyprOpenGL->clear(CHyprColor{0, 0, 0, 1.0f});

  double scale = std::min(fb.m_size.x / surfaceSize.x, fb.m_size.y / surfaceSize.y);

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

  // fade in
  if (!ready) {
    textureAlpha.set(1.0f, false);
  }
  ready = true;
  lastUpdated = std::chrono::steady_clock::now();
}

void WindowSnapshot::draw(const Vector2D &offset) {
  Log::logger->log(Log::TRACE, "[{}] WindowSnapshot::draw", PLUGIN_NAME);
  auto tex = fb.getTexture();
  CBox box = {pos, size};
  box.translate(offset);
  if (!tex)
    g_pHyprOpenGL->renderRect(box, CHyprColor{0, 0, 0, alphaAbs()}, {.round = 0});
  else
    g_pHyprOpenGL->renderTexture(tex, box, {.a = alphaAbs()});
}

void TextBox::update(const double delta) {
  Log::logger->log(Log::TRACE, "[{}] TextBox::update", PLUGIN_NAME);
  if (size.x <= 1 || size.y <= 1) {
    return;
  }
  if (window->m_title != lasttitle) {
    lasttitle = window->m_title;
    // 0.6 might be correct?
    auto div = std::max(1.0, (FONTSIZE * 0.6));
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
    texture = g_pHyprOpenGL->renderText(text, color, FONTSIZE);
  }
  if (!texture) {
    Log::logger->log(Log::ERR, "[{}] TextBox::draw, no texture", PLUGIN_NAME);
    return;
  }
  auto textScale = size.y / texture->m_size.y;
  auto width = texture->m_size.x * textScale;
  float center = (size.x - width) / 2.0f;
  if (center < 0)
    center = 0;
  auto box = CBox{offset.x + center, offset.y, width, size.y};
  g_pHyprOpenGL->renderTexture(texture, box, {.a = alphaAbs()});
}

void WindowContainer::update(const double delta) {
  Log::logger->log(Log::TRACE, "[{}] WindowContainer::update", PLUGIN_NAME);

  if (!header || !snapshot) {
    Log::logger->log(Log::ERR, "[{}] WindowContainer::update missing sub-elements", PLUGIN_NAME);
    return;
  }

  const Vector2D curSize = animSize.current;

  header->pos = Vector2D(0, 0);
  header->size = Vector2D(curSize.x, (double)header->fontsize);

  snapshot->pos = Vector2D(0.0, header->size.y);
  snapshot->size = Vector2D(curSize.x, curSize.y - header->size.y);

  border->pos = Vector2D(0, 0);
  border->size = Vector2D(curSize.x, curSize.y);
  /* Silly button for later.
    // const auto pad = 4;
    closeButton->size = {header->fontsize, header->fontsize};
    // closeButton->size -= {pad * 2, pad * 2};
    closeButton->pos = Vector2D((int)(curSize.x - (closeButton->size.x)), 0);
    // closeButton->pos = Vector2D((int)(curSize.x - (closeButton->size.x + pad * 2)), 0);
    //  closeButton->pos += {pad, pad};

    closeButton->label->size = closeButton->size;
    closeButton->label->pos = closeButton->pos;
  */
  Log::logger->log(Log::TRACE, "[{}] WindowContainer::update, closeButton size: {}", PLUGIN_NAME, closeButton->size);
  Container::update(delta);
}

void WindowContainer::draw(const Vector2D &offset) {
  Log::logger->log(Log::TRACE, "[{}] WindowContainer::draw", PLUGIN_NAME);
  Container::draw(offset.round());
}

bool WindowContainer::onMouseClick(const Vector2D &mousePos) {
  if (closeButton && closeButton->onMouseClick(mousePos)) {
    Log::logger->log(Log::ERR, "Close button clicked!");
    return true;
  }

  if (Element::onMouseClick(mousePos)) {
    if (!g_pCarouselManager) {
      Log::logger->log(Log::ERR, "CarouselManager not initialized");
      return false;
    }
    g_pCarouselManager->updateSelection(this);
    return true;
  }

  return false;
}
WindowContainer::WindowContainer(PHLWINDOW window) : window(window) {
  Log::logger->log(Log::TRACE, "[{}] WindowContainer::WindowContainer", PLUGIN_NAME);
  header = add<TextBox>(window, TITLECOLOR, FONTSIZE);
  snapshot = add<WindowSnapshot>(window);
  border = add<BorderBox>(window, BORDERSIZE, BORDERROUNDING, BORDERROUNDINGPOWER);
  /* Button thing
    auto closeLabel = makeUnique<Label>("X", CHyprColor{1.0f, 1.0f, 1.0f, 1.0f}, FONTSIZE);
    closeButton = add<Button>(CHyprColor{0.8f, 0.0f, 0.0f, 0.6f}, [this]() { g_pCompositor->closeWindow(this->window); }, std::move(closeLabel));
    */
  alpha = 1.0f;
};

void BorderBox::draw(const Vector2D &offset) {
  Log::logger->log(Log::TRACE, "[{}] BorderBox::draw", PLUGIN_NAME);
  if (size.x <= 1 || size.y <= 1) {
    Log::logger->log(Log::ERR, "[{}] BorderBox::draw, invalid size: {}", PLUGIN_NAME, size);
    return;
  }
  auto box = CBox{offset, size}.round();
  g_pHyprOpenGL->renderBorder(box, isActive ? *ACTIVEBORDERCOLOR : *INACTIVEBORDERCOLOR, {.round = rounding, .roundingPower = power, .borderSize = bordersize, .a = alphaAbs()});
}
void BorderBox::update(const double delta) { alpha.tick(delta, ANIMATIONSPEED); }

void Label::draw(const Vector2D &offset) {
  if (size.x <= 1 || size.y <= 1) {
    Log::logger->log(Log::ERR, "[{}] Label::draw, invalid size: {}", PLUGIN_NAME, size);
    return;
  }

  const auto pad = 1;
  pos += offset;
  // pos += {pad, pad};
  // size -= {pad * 3, pad * 2};

  if (!texture)
    texture = g_pHyprOpenGL->renderText(text, color, fontsize, false, "", 1, 800);
  if (!texture)
    return;
  g_pHyprOpenGL->renderTexture(texture, CBox{pos, size}, {});
}

void Button::draw(const Vector2D &offset) {
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
  if (label) {
    label->draw(offset);
  }
}

bool Button::onMouseClick(const Vector2D &mousePos) {
  if (Element::onMouseClick(mousePos) && onClick) {
    onClick();
    return true;
  }
  return false;
}

void Button::onHoverChanged() {
  if (hovered) {
    scale.target = 1.1f;
    alpha.target = 1.0f;
  } else {
    scale.target = 1.0f;
    alpha.target = 0.8f;
  }
}
Button::Button(CHyprColor col, std::function<void()> callback, UP<Label> label) : color(col), onClick(callback), label(std::move(label)) {
  if (label) {
    label->setParent(this);
  }
}
