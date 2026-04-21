#include "defines.hpp"
#include "logger.hpp"
#include <hyprutils/math/Vector2D.hpp>

#define private public
#define protected public
#include <src/render/OpenGL.hpp>
#include <src/render/Renderer.hpp>
#undef protected
#undef private

#include "manager.hpp"
#include "monitor.hpp"

#include <src/Compositor.hpp>
#include <src/desktop/state/FocusState.hpp>
#include <src/desktop/view/Window.hpp>
#include <src/render/pass/RectPassElement.hpp>
#include <src/render/pass/TexPassElement.hpp>

#include <src/protocols/PresentationTime.hpp>

using namespace alttab;

alttab::Monitor::Monitor(PHLMONITOR monitor) : monitor(monitor),
                                               alpha(&Config::monitorAnimationSpeed),
                                               rotation(&Config::rotationSpeed),
                                               zoom(&Config::monitorAnimationSpeed) {
  createTexture();
  rotation.snap(M_PI / 2.0f);
  if (isActive()) {
    zoom.snap(1.0f);
    alpha.snap(1.0f);
  } else {
    zoom.snap(0.1f);
    alpha.snap(0.1f);
  }
}
void alttab::Monitor::createTexture() {
  LOG_SCOPE()
  bgFb = makeShared<CFramebuffer>();
  blurFb = makeShared<CFramebuffer>();
  if (monitor->m_pixelSize.x <= 0 || monitor->m_pixelSize.y <= 0)
    return;

  if (!bgFb->isAllocated() || bgFb->m_size != monitor->m_pixelSize)
    bgFb->alloc(monitor->m_pixelSize.x, monitor->m_pixelSize.y, monitor->m_drmFormat);
  CRegion fullRegion = CBox({0, 0}, monitor->m_pixelSize);

  OVERRIDE_WORKSPACE = false;
  g_pHyprRenderer->beginRender(monitor, fullRegion, RENDER_MODE_FULL_FAKE, {}, bgFb.get());
  g_pHyprRenderer->renderWorkspace(monitor, monitor->m_activeWorkspace, Time::steadyNow(), fullRegion.getExtents());
  g_pHyprRenderer->m_renderPass.render(fullRegion);
  g_pHyprRenderer->m_renderPass.clear();
  g_pHyprRenderer->endRender();
  OVERRIDE_WORKSPACE = true;
  texture = bgFb->getTexture();

  if (!blurFb->isAllocated() || blurFb->m_size != monitor->m_pixelSize / 2)
    blurFb->alloc(monitor->m_pixelSize.x / 2, monitor->m_pixelSize.y / 2, monitor->m_drmFormat);
  CRegion blurRegion = CBox({0, 0}, monitor->m_pixelSize);

  g_pHyprRenderer->beginRender(monitor, blurRegion, RENDER_MODE_FULL_FAKE, {}, blurFb.get());

  CBox destBox = {{0, 0}, monitor->m_pixelSize / 2};
  CTexPassElement::SRenderData data;
  data.tex = texture;
  data.box = destBox;
  data.blur = true;
  g_pHyprRenderer->m_renderPass.add(makeUnique<CTexPassElement>(data));

  // Blur trigger — deferred via CRectPassElement
  {
    CRectPassElement::SRectData rect;
    rect.box = destBox;
    rect.color = {0.0, 0.0, 0.0, 0.0};
    rect.blur = true;
    g_pHyprRenderer->m_renderPass.add(makeUnique<CRectPassElement>(rect));
  }

  g_pHyprRenderer->m_renderPass.render(blurRegion);
  g_pHyprRenderer->m_renderPass.clear();
  g_pHyprRenderer->endRender();
  blurred = blurFb->getTexture();
}

WP<WindowCard> alttab::Monitor::addWindow(PHLWINDOW window) {
  auto w = makeUnique<WindowCard>(window);
  windows.emplace_back(std::move(w));
  return windows.back();
}
size_t alttab::Monitor::removeWindow(PHLWINDOW window) {
  std::erase_if(windows, [&](const auto &card) {
    return card->window == window;
  });
  return windows.size();
}

void alttab::Monitor::update(const float delta, const float offset, CRegion &damage) {
  LOG_SCOPE(Log::UPDATE)
  const auto MONITOR = Desktop::focusState()->monitor();

  zoom.set(isActive() ? 1.0f : 0.1f, false);
  alpha.set(isActive() ? 1.0f : 0.1f, false);

  const size_t count = windows.size();
  if (count == 0)
    return;

  const float invCount = 1.0f / sc<float>(count);
  const float r = (MONITOR->m_size.x * 0.5f) * Config::carouselSize.value_or(1.0f);

  auto ctx = StyleContext{
      .count = count,
      .active = activeWindow,
      .invCount = invCount,
      .angleStep = (2.0f * (float)M_PI) * invCount,
      .mSize = MONITOR->m_size,
      .midpoint = MONITOR->m_size * 0.5f,
      .radius = r,
      .tiltOffset = r * std::sin(Config::tilt * ((float)M_PI / 180.0f)),
      .rotation = rotation.current,
      .scale = zoom.current,
      .alpha = alpha.current,
      .activeProgress = std::clamp(rotation.progress, 0.0f, 1.0f)};

  const size_t winCount = windows.size();
  renderTasks.clear();
  if (winCount > renderTasks.capacity())
    renderTasks.reserve(winCount);

  for (size_t i = 0; i < winCount; ++i) {
    //Guard against expired window handles
    if (!windows[i] || !windows[i]->window || !windows[i]->window->wlSurface())
      continue;

    RenderData data = manager->layoutStyle->calculate(ctx, windows[i]->window->m_size, i);
    if (!data.visible)
      continue;

    data.position.translate({0, (int)offset}).round();
    windows[i]->setPosition(data.position);

    renderTasks.emplace_back(RenderTask{windows[i].get(), data, 0.0f});
  }

  std::stable_sort(renderTasks.begin(), renderTasks.end(), [](const auto &a, const auto &b) {
    return a.data.z > b.data.z;
  });

  //Unified bounding-box damage instead of per-card expansion
  CRegion usedArea;
  double minX = 1e9, minY = 1e9, maxX = -1e9, maxY = -1e9;

  for (auto &task : renderTasks) {
    CRegion visible = CRegion(task.data.position).subtract(usedArea);
    if (visible.empty()) {
      task.visibility = 0.0f;
      continue;
    }

    double area = 0;
    visible.forEachRect([&area](const pixman_box32_t &r) {
      area += (double)(r.x2 - r.x1) * (r.y2 - r.y1);
    });

    task.visibility = std::clamp((float)(area / (task.data.position.width * task.data.position.height)), 0.0f, 1.0f);

    // Accumulate this window's footprint into the occlusion mask for subsequent (background) windows
    usedArea.add(task.data.position);

    // Accumulate global bounds (padded for blur kernel headroom)
    CBox outerBox = task.data.position.copy();
    outerBox.round();
    outerBox.expand(Config::borderSize + 32);
    minX = std::min(minX, outerBox.x);
    minY = std::min(minY, outerBox.y);
    maxX = std::max(maxX, outerBox.x + outerBox.width);
    maxY = std::max(maxY, outerBox.y + outerBox.height);
  }

  // Submit a single unified damage rectangle
  if (minX < maxX && minY < maxY) {
    cachedGlobalBounds = CBox{minX, minY, maxX - minX, maxY - minY};
    cachedGlobalBounds.round();
    damage.add(cachedGlobalBounds);
  }
}

void alttab::Monitor::draw(const CRegion &damage, const float alpha) {
  LOG_SCOPE(Log::DRAW)

  for (auto &task : renderTasks | std::views::reverse) {
    // Guard card and window validity
    if (!task.card || !task.card->window)
      continue;
    task.card->draw(damage);
    if (Config::livePreview && task.visibility > Config::previewCutoff)
      task.card->present();
  }
}

void alttab::Monitor::activeChanged() {
  LOG_SCOPE()
  const int count = windows.size();
  if (count <= 0) {
    LOG(Log::UPDATE, "count <= 0");
    return;
  }

  LOG(Log::UPDATE, "activeWindow1: {}, size: {}", activeWindow, count);

  for (auto i = 0; i < windows.size(); ++i) {
    windows[i]->isActive = (i == activeWindow);
  }
  LOG(Log::UPDATE, "activeWindow2: {}, size: {}", activeWindow, count);
  // Why am i doing this backwards?? stilling figuring out
  const auto target = (M_PI / 2) + (M_PI * 2.0f * activeWindow) / count;
  auto diff = target - rotation.target;
  diff = std::remainder(diff, 2.0f * M_PI);

  rotation.set(rotation.target + diff, false);
}

bool alttab::Monitor::isActive() const {
  LOG_SCOPE()
  return manager->activeMonitor == monitor->m_id;
}
