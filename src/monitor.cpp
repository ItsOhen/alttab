#include "monitor.hpp"
#include "defines.hpp"
#include "manager.hpp"
#include <src/Compositor.hpp>
#include <src/desktop/state/FocusState.hpp>
#include <src/desktop/view/Window.hpp>
#include <src/render/pass/RectPassElement.hpp>
#include <src/render/pass/TexPassElement.hpp>
#define private public
#include <src/render/OpenGL.hpp>
#include <src/render/Renderer.hpp>
#undef private

#include <src/protocols/PresentationTime.hpp>

Monitor::Monitor(PHLMONITOR monitor) : monitor(monitor) {
  createTexture();
  activeWindow = 0;
  rotation.snap(M_PI / 2.0f);
  animating = false;
}
void Monitor::createTexture() {
  LOG_SCOPE()
  g_pHyprRenderer->makeEGLCurrent();

  if (monitor->m_pixelSize.x <= 0 || monitor->m_pixelSize.y <= 0)
    return;

  if (!bgFb.isAllocated() || bgFb.m_size != monitor->m_pixelSize)
    bgFb.alloc(monitor->m_pixelSize.x, monitor->m_pixelSize.y, monitor->m_drmFormat);
  CRegion fullRegion = CBox({0, 0}, monitor->m_pixelSize);

  g_pHyprRenderer->beginRender(monitor, fullRegion, RENDER_MODE_FULL_FAKE, nullptr, &bgFb, false);
  g_pHyprRenderer->renderWorkspace(monitor, monitor->m_activeWorkspace, NOW, fullRegion.getExtents());
  g_pHyprRenderer->m_renderPass.render(fullRegion);
  g_pHyprRenderer->m_renderPass.clear();
  g_pHyprRenderer->endRender();
  texture = bgFb.getTexture();

  if (!blurFb.isAllocated() || blurFb.m_size != monitor->m_pixelSize / 2)
    blurFb.alloc(monitor->m_pixelSize.x / 2, monitor->m_pixelSize.y / 2, monitor->m_drmFormat);
  CRegion blurRegion = CBox({0, 0}, monitor->m_pixelSize);

  g_pHyprRenderer->beginRender(monitor, blurRegion, RENDER_MODE_FULL_FAKE, nullptr, &blurFb, false);

  CBox destBox = {{0, 0}, monitor->m_pixelSize / 2};
  CTexPassElement::SRenderData data;
  data.tex = texture;
  data.box = destBox;
  data.blur = true;
  g_pHyprRenderer->m_renderPass.add(makeUnique<CTexPassElement>(data));
  CRectPassElement::SRectData blur;
  blur.box = destBox;
  blur.color = {0.0, 0.0, 0.0, 0.0};
  blur.blur = true;
  g_pHyprRenderer->m_renderPass.add(makeUnique<CRectPassElement>(blur));

#ifndef NDEBUG
  CRectPassElement::SRectData debug;
  debug.box = destBox;
  debug.color = {0.0, 0.0, 1.0, 0.5};
  g_pHyprRenderer->m_renderPass.add(makeUnique<CRectPassElement>(debug));
#endif
  g_pHyprRenderer->m_renderPass.render(blurRegion);
  g_pHyprRenderer->m_renderPass.clear();
  g_pHyprRenderer->endRender();
  blurred = blurFb.getTexture();
}

void Monitor::renderTexture(const CRegion &damage) {
  if (!blurred || !texture || !monitor) {
    LOG(ERR, "FAILED: (!blurred || !texture || !monitor)");
    return;
  }

  auto dmg = damage;
  const auto box = CBox{{0, 0}, monitor->m_pixelSize};
  /* Need a better way to do this
    if (DIMENABLED)
    g_pHyprOpenGL->renderRect(dmg.getExtents(), {0, 0, 0, DIMAMOUNT}, {});
  */
  if (Config::blurBG)
    g_pHyprOpenGL->renderTexture(blurred, box, {});
  else if (Config::powersave)
    g_pHyprOpenGL->renderTexture(texture, box, {});
}

WP<WindowCard> Monitor::addWindow(PHLWINDOW window) {
  auto w = makeUnique<WindowCard>(window);
  windows.emplace_back(std::move(w));
  return windows.back();
}
size_t Monitor::removeWindow(PHLWINDOW window) {
  std::erase_if(windows, [&](const auto &card) {
    return card->window == window;
  });
  return windows.size();
}

bool Monitor::animate(const float delta) {
  const auto active = isActive();
  zoom.set(active ? 1.0f : 0.1f, false);
  alpha.set(active ? 1.0f : 0.1f, false);
  rotation.tick(delta, Config::rotationSpeed);
  zoom.tick(delta, Config::monitorAnimationSpeed);
  alpha.tick(delta, Config::monitorAnimationSpeed);
  return !rotation.done() || !zoom.done() || !alpha.done();
}

void Monitor::update(const float delta) {
  const auto MONITOR = Desktop::focusState()->monitor();
  const Vector2D mSize = MONITOR->m_size * MONITOR->m_scale;

  bool damage = animate(delta);

  renderTasks.clear();
  for (size_t i = 0; i < windows.size(); ++i) {
    auto ctx = StyleContext{i, windows.size(), activeWindow, rotation.current, zoom.current, alpha.current, mSize, {0, 0}};
    auto surfaceSize = windows[i]->window->wlSurface()->getSurfaceBoxGlobal().value_or(CBox{0, 0, 0, 0}).size();
    auto data = manager->layoutStyle->calculate(ctx, surfaceSize);

    // TODO: get rid of this and do proper depth and clip
    const auto padding = 4;
    const auto barHeight = (Config::fontSize + padding) * data.scale;
    double pX1 = data.position.x, pY1 = data.position.y + barHeight;
    double pW = data.position.width, pH = data.position.height - barHeight;

    double interW = std::max(0.0, std::min(mSize.x, pX1 + pW) - std::max(0.0, pX1));
    double interH = std::max(0.0, std::min(mSize.y, pY1 + pH) - std::max(0.0, pY1));
    float visibility = (pW * pH > 0) ? (float)((interW * interH) / (pW * pH)) : 0.0f;

    renderTasks.emplace_back(RenderTask{windows[i].get(), data, visibility, FloatTime(NOW - windows[i]->lastSnapshot).count()});
  }

  std::vector<RenderTask *> snapshotRR;
  for (auto &t : renderTasks)
    snapshotRR.emplace_back(&t);

  std::sort(snapshotRR.begin(), snapshotRR.end(), [](const RenderTask *a, const RenderTask *b) {
    return a->since > b->since;
  });

  int snapshotsDone = 0;
  for (auto task : snapshotRR) {
    if (task->visibility > 0.10f && (!task->card->ready || task->since > 0.5f)) {
      if (snapshotsDone < 2) {
        task->card->requestFrame(MONITOR);
        task->card->snapshot(mSize * Config::windowSize);
        snapshotsDone++;
        damage = true;
      }
    }
  }

  animating = damage;
}

void Monitor::draw(const CRegion &damage, const float &offset, const float alpha = 1.0f) {
  if (!monitor)
    return;

  if (renderTasks.empty())
    return;

  // Sorting by Z is fine here; it only affects the painter's algorithm order
  std::sort(renderTasks.begin(), renderTasks.end(), [](const auto &a, const auto &b) {
    return a.data.z < b.data.z;
  });

  auto dmg = damage;
  for (const auto &task : renderTasks) {
    auto box = task.data.position;
    box.translate({0.0f, offset});
    dmg = dmg.intersect(box);
    task.card->draw(box, task.data.scale, std::min(task.data.alpha, alpha));
  }
#ifndef NDEBUG
  if (!dmg.empty())
    g_pHyprOpenGL->renderRect(dmg.getExtents(), {0.5, 0.5, 0.0, 0.5}, {});
#endif
}

void Monitor::activeChanged() {
  const int count = windows.size();
  if (count <= 0)
    return;

  // Why am i doing this backwards??
  const auto target = (M_PI / 2) + (M_PI * 2.0f * activeWindow) / count;
  auto diff = target - rotation.target;
  diff = std::remainder(diff, 2.0f * M_PI);

  rotation.set(rotation.target + diff, false);
}

bool Monitor::isActive() const {
  return manager->activeMonitor == monitor->m_id;
}
