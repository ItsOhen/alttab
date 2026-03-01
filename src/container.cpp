#include "container.hpp"
#include "helpers.hpp"
#include <hyprutils/math/Vector2D.hpp>
#include <src/desktop/state/FocusState.hpp>
#include <src/desktop/view/Window.hpp>
#include <src/helpers/Color.hpp>
#include <src/render/Renderer.hpp>

WindowCard::WindowCard(PHLWINDOW window) : window(window) {
  commitListener = window->wlSurface()->resource()->m_events.commit.listen([this]() {
    LOG_SCOPE();
    this->needsRefresh = true;
    this->ready = false;
  });
  borderColor = CGradientValueData(Colors::YELLOW);
}

WindowCard::~WindowCard() {
  commitListener.reset();
  if (fboID) {
    g_pHyprRenderer->makeEGLCurrent();
    glDeleteFramebuffers(1, &fboID);
  }
}

void WindowCard::draw(const CBox &box, const float scale, const float alpha = 1.0f) {
  LOG_SCOPE();
  const auto FONTSIZE = *CConfigValue<Hyprlang::INT>("plugin:alttab:font_size");

  contentBox = box;
  contentBox.round();
  contentBox = contentBox.expand(-borderSize);
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
#endif
}

bool WindowCard::initGL() {
  if (fboID != 0)
    return true;

  g_pHyprRenderer->makeEGLCurrent();
  glGenFramebuffers(1, &fboID);

  return fboID != 0;
}

void WindowCard::snapshot(const Vector2D &targetSize) {
  if (!window || !window->wlSurface() || !window->wlSurface()->resource())
    return;
  if (!initGL())
    return;
  if (targetSize.x <= 1 || targetSize.y <= 1)
    return;

  g_pHyprRenderer->makeEGLCurrent();
  LOG(ERR, "snapshot: ({}) {} {}", window->m_title, targetSize.x, targetSize.y);

  const auto MON = Desktop::focusState()->monitor();
  bool isEmpty = (fb.m_size.x == 0 || fb.m_size.y == 0);
  bool sizeChanged = std::abs(targetSize.x - fb.m_size.x) > 2 || std::abs(targetSize.y - fb.m_size.y) > 2;

  if (isEmpty || sizeChanged) {
    fb.alloc(targetSize.x, targetSize.y, MON->m_drmFormat);
  }

  glBindFramebuffer(GL_READ_FRAMEBUFFER, fboID);
  glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fb.m_fb);

  glClearColor(0, 0, 0, 0);
  glClear(GL_COLOR_BUFFER_BIT);

  const auto surface = window->wlSurface()->getSurfaceBoxGlobal().value_or(CBox{0, 0, 0, 0}).size();
  if (surface.x <= 0 || surface.y <= 0)
    return;

  const auto scale = fb.m_size / surface;

  window->wlSurface()->resource()->breadthfirst([&](SP<CWLSurfaceResource> s, const Vector2D &offset, void *) {
    auto &tex = s->m_current.texture;
    if (!tex || tex->m_texID == 0 || s->m_current.scale <= 0)
      return;

    glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex->m_texID, 0);

    const auto logical = tex->m_size / s->m_current.scale;
    const auto dst1 = offset * scale;
    const auto dst2 = dst1 + (logical * scale);

    glBlitFramebuffer(0, 0, tex->m_size.x, tex->m_size.y,
                      dst1.x, dst1.y, dst2.x, dst2.y,
                      GL_COLOR_BUFFER_BIT, GL_LINEAR);
  },
                                                nullptr);

  glBindTexture(GL_TEXTURE_2D, fb.getTexture()->m_texID);
  glGenerateMipmap(GL_TEXTURE_2D);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, 4.0f);
  glBindTexture(GL_TEXTURE_2D, 0);

  lastCommit = NOW;
  ready = true;
}

void WindowCard::drawTitle(const CBox &box, const float scale, const float alpha) {
  const auto FONTSIZE = *CConfigValue<Hyprlang::INT>("plugin:alttab:font_size");
  float baseWidth = box.width / scale;
  float padding = 10.0f;

  if (window->m_title != title || std::abs(baseWidth - lastWidth) > 1.0f) {
    title = window->m_title;
    lastWidth = baseWidth;

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
  LOG(ERR, "borderpass: ({}), alpha: {}", window->m_title, alpha);
  const auto BORDERSIZE = *CConfigValue<Hyprlang::INT>("plugin:alttab:border_size");
  const auto BORDERROUNDING = *CConfigValue<Hyprlang::INT>("plugin:alttab:border_rounding");
  const auto BORDERROUNDINGPOWER = *CConfigValue<Hyprlang::FLOAT>("plugin:alttab:border_rounding_power");
  const auto ACTIVEBORDERCOLOR = rc<CGradientValueData *>(std::any_cast<void *>(HyprlandAPI::getConfigValue(PHANDLE, "plugin:alttab:border_active")->getValue()));
  const auto INACTIVEBORDERCOLOR = rc<CGradientValueData *>(std::any_cast<void *>(HyprlandAPI::getConfigValue(PHANDLE, "plugin:alttab:border_inactive")->getValue()));
  g_pHyprOpenGL->renderBorder(contentBox, isActive ? *ACTIVEBORDERCOLOR : *INACTIVEBORDERCOLOR, {.round = sc<int>(BORDERROUNDING), .roundingPower = sc<float>(BORDERROUNDINGPOWER), .borderSize = sc<int>(BORDERSIZE), .a = alpha});
}
