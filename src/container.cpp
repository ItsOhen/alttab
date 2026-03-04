#include "container.hpp"
#include "helpers.hpp"
#include <hyprutils/math/Vector2D.hpp>
#include <src/desktop/state/FocusState.hpp>
#include <src/desktop/view/Window.hpp>
#include <src/helpers/Color.hpp>
#include <src/protocols/PresentationTime.hpp>
#include <src/render/Renderer.hpp>

WindowCard::WindowCard(PHLWINDOW window) : window(window) {
  attachListeners(window->resource());
  lastCommit = lastSnapshot = NOW;
}

WindowCard::~WindowCard() {
  commit.clear();
}

void WindowCard::attachListeners(SP<CWLSurfaceResource> surface) {
  if (!surface)
    return;

  surface->breadthfirst([this](SP<CWLSurfaceResource> s, const Vector2D &offset, void *data) {
    commit.push_back(s->m_events.commit.listen([this] {
      const auto since = NOW - this->lastCommit;
      if (FloatTime(since).count() > 0.0016) {
        LOG(ERR, "In commit for: {}, since: {}", this->window->m_title, FloatTime(since).count());
        this->ready = false;
        this->lastCommit = NOW;
      }
    }));
  },
                        nullptr);
}

void WindowCard::requestFrame(PHLMONITOR monitor) {
  LOG(ERR, "{}: mapped: {}, ready: {}", window->m_title, window->resource()->m_mapped, ready);
  if (!window->resource())
    return;

  window->resource()->breadthfirst([&](SP<CWLSurfaceResource> s, const Vector2D &offset, void *data) {
    s->frame(NOW);
    s->presentFeedback(NOW, monitor, false);
  },
                                   nullptr);
}

void WindowCard::draw(const CBox &box, const float scale, const float alpha = 1.0f) {
  LOG_SCOPE();

  contentBox = box;
  contentBox.round();
  contentBox = contentBox.expand(-BORDERSIZE);
  const auto padding = 4;
  const auto barHeight = (FONTSIZE + padding) * scale;
  titleBox = {contentBox.x, contentBox.y, contentBox.width, barHeight};
  previewBox = {contentBox.x, contentBox.y + barHeight, contentBox.width, contentBox.height - barHeight};
  /* Maybe..
  auto shadowBox = box;
  shadowBox.round();
  static const auto DROPSHADOW = 2;
  shadowBox.expand(DROPSHADOW);
  g_pHyprOpenGL->renderRoundedShadow(shadowBox, 2, 2, DROPSHADOW * 2, Colors::BLACK, 0.4f);
  */
  drawTitle(box, scale, alpha);
  drawBorder(alpha);
  if (!fb.m_fb) {
    g_pHyprOpenGL->renderRect(previewBox, CHyprColor(0.0, 0.0, 0.0, alpha), {});
  } else {
    auto texture = fb.getTexture();
    if (!texture) {
      LOG(ERR, "texture: nullptr");
      return;
    }
    LOG(ERR, "texpass: ({}) alpha: {}", window->m_title, alpha);
    g_pHyprOpenGL->renderRect(previewBox, CHyprColor(0.0, 0.0, 0.0, 1.0 * alpha), {});
    g_pHyprOpenGL->renderTexture(texture, previewBox, {.a = alpha});
  }
#ifndef NDEBUG
  g_pHyprOpenGL->renderRect(contentBox, CHyprColor(1.0, 0.0, 0.0, 0.2), {});
  auto text = g_pHyprOpenGL->renderText(std::format("Time: {} ms", std::chrono::duration_cast<std::chrono::milliseconds>(NOW - lastCommit).count()), CHyprColor(1.0, 1.0, 1.0, 1.0), 20);
  g_pHyprOpenGL->renderTexture(text, {contentBox.pos(), text->m_size}, {.a = 1.0});
#endif
}

bool WindowCard::snapshot(const Vector2D &targetSize) {
  LOG_SCOPE(Log::ERR);
  if (!window || !window->wlSurface() || !window->wlSurface()->resource()) {
    LOG(ERR, "No window or surface");
    return false;
  }
  if (targetSize.x <= 1 || targetSize.y <= 1) {
    LOG(ERR, "targetSize.x ({}) <= 1 || targetSize.y ({}) <= 1", targetSize.x, targetSize.y);
    return false;
  }

  if (targetSize.x <= 1 || targetSize.y <= 1)
    return false;

  const auto resource = window->wlSurface()->resource();
  if (!resource->m_current.updated.all && !firstSnapshot)
    return false;
  if (firstSnapshot)
    firstSnapshot = false;

  const auto MONITOR = Desktop::focusState()->monitor();
  auto surfaceSize = window->wlSurface()->getSurfaceBoxGlobal().value_or({0, 0, 0, 0}).size();
  CRegion fakeDamage = CBox{{0, 0}, targetSize};

  if (surfaceSize.x < 1.0 || surfaceSize.y < 1.0) {
    Log::logger->log(Log::ERR, "[{}] WindowSnapshot::update, invalid surface size: {}", PLUGIN_NAME, surfaceSize);
    return false;
  }

  if (targetSize > fb.m_size) {
    fb.alloc(targetSize.x, targetSize.y, MONITOR->m_output->state->state().drmFormat);
  }

  g_pHyprRenderer->makeEGLCurrent();

  if (!g_pHyprRenderer->beginRender(MONITOR, fakeDamage, RENDER_MODE_FULL_FAKE, nullptr, &fb)) {
    return false;
  }

  g_pHyprOpenGL->clear(CHyprColor{0, 0, 0, 1.0f});

  const double scale = std::min(fb.m_size.x / surfaceSize.x, fb.m_size.y / surfaceSize.y);
  g_pHyprRenderer->m_bBlockSurfaceFeedback = true;
  resource->breadthfirst([&](SP<CWLSurfaceResource> s, const Vector2D &offset, void *) {
    if (!s->m_current.texture)
      return;
    auto box = s->extends();
    box.scale(scale).translate(offset * scale);
    g_pHyprOpenGL->renderTexture(s->m_current.texture, box, {.a = 1.0f});
  },
                         nullptr);

  g_pHyprRenderer->m_bBlockSurfaceFeedback = false;
  g_pHyprRenderer->endRender();
  lastSnapshot = NOW;
  ready = true;
  return true;
}

void WindowCard::drawTitle(const CBox &box, const float scale, const float alpha) {
  float baseWidth = box.width / scale;
  float padding = 10.0f;

  if (window->m_title != title) {
    title = window->m_title;
    int maxChars = std::max(5.0f, (float)((baseWidth - padding) / (FONTSIZE * 0.55f)));
    std::string displayTitle = middleTruncate(title, maxChars);
    titleTexture = g_pHyprOpenGL->renderText(displayTitle, CHyprColor(1.0, 1.0, 1.0, 1.0), FONTSIZE);
  }

  g_pHyprOpenGL->renderRect(titleBox, CHyprColor(0.0, 0.0, 0.0, 0.8 * alpha), {});

  if (!titleTexture)
    return;

  Vector2D dSize = titleTexture->m_size * scale;
  Vector2D dPos = titleBox.pos() + (titleBox.size() - dSize) * 0.5f;
  g_pHyprOpenGL->renderTexture(titleTexture, {dPos, dSize}, {.a = alpha});
}

void WindowCard::drawBorder(const float alpha) {
  g_pHyprOpenGL->renderBorder(contentBox, isActive ? *ACTIVEBORDERCOLOR : *INACTIVEBORDERCOLOR, {.round = BORDERROUNDING, .roundingPower = BORDERROUNDINGPOWER, .borderSize = BORDERSIZE, .a = alpha});
}
