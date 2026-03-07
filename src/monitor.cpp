#include "monitor.hpp"
#include "defines.hpp"
#include "logger.hpp"
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
  rotation.snap(M_PI / 2.0f);
  if (isActive()) {
    zoom.snap(1.0f);
    alpha.snap(1.0f);
  } else {
    zoom.snap(0.1f);
    alpha.snap(0.1f);
  }

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
    LOG(Log::DRAW, "FAILED: (!blurred || !texture || !monitor)");
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
  LOG_SCOPE(Log::ANIMATE)
  const auto active = isActive();
  zoom.set(active ? 1.0f : 0.1f, false);
  alpha.set(active ? 1.0f : 0.1f, false);
  rotation.tick(delta, Config::rotationSpeed);
  zoom.tick(delta, Config::monitorAnimationSpeed);
  alpha.tick(delta, Config::monitorAnimationSpeed);
  return !rotation.done() || !zoom.done() || !alpha.done();
}

void Monitor::update(const float delta, const Vector2D &offset) {
  LOG_SCOPE(Log::UPDATE)
  const auto MONITOR = Desktop::focusState()->monitor();
  const Vector2D mSize = MONITOR->m_size * MONITOR->m_scale;

  bool damage = animate(delta);
  renderTasks.clear();

  for (size_t i = 0; i < windows.size(); ++i) {
    auto ctx = StyleContext{i, windows.size(), activeWindow, rotation.current, zoom.current, alpha.current, mSize, {0, 0}};
    auto surfaceSize = windows[i]->window->wlSurface()->getSurfaceBoxGlobal().value_or(CBox{0, 0, 0, 0}).size();
    auto data = manager->layoutStyle->calculate(ctx, surfaceSize);
    auto pos = data.position;
    pos.translate(offset);
    windows[i]->setPosition(pos);
    if (!data.visible)
      continue;
    renderTasks.emplace_back(RenderTask{windows[i].get(), data, 0.0f, FloatTime(NOW - windows[i]->lastSnapshot).count()});
  }

  std::sort(renderTasks.begin(), renderTasks.end(), [](const auto &a, const auto &b) {
    return a.data.z > b.data.z;
  });

  CRegion usedArea;
  std::vector<RenderTask *> snapshotRR;

  for (auto &task : renderTasks) {
    CRegion visible = CRegion(task.data.position).subtract(usedArea);
    if (visible.empty())
      continue;
    double area = 0;
    visible.forEachRect([&area](const pixman_box32_t &r) {
      area += (double)(r.x2 - r.x1) * (r.y2 - r.y1);
    });

    task.visibility = std::clamp((float)(area / (task.data.position.width * task.data.position.height)), 0.0f, 1.0f);
    if ((task.visibility > 0.20f || task.card->firstSnapshot) && (!task.card->ready || task.since > 0.5f)) {
      snapshotRR.emplace_back(&task);
    }
    usedArea.add(task.data.position);
  }

  std::sort(snapshotRR.begin(), snapshotRR.end(), [](const RenderTask *a, const RenderTask *b) {
    if (a->card->firstSnapshot != b->card->firstSnapshot)
      return a->card->firstSnapshot;

    if (a->card->z != b->card->z)
      return a->card->z > b->card->z;

    return a->since > b->since;
  });

  int snapshotsDone = 0;
  for (auto *task : snapshotRR) {
    if (snapshotsDone >= 4)
      break;

    task->card->requestFrame(MONITOR);
    bool updated = task->card->snapshot(mSize * Config::windowSize);
    if (updated) {
      snapshotsDone++;
      damage = true;
    } else if (!task->card->firstSnapshot) {
      task->card->lastSnapshot = NOW;
    }
  }

  animating = damage;
}

void Monitor::draw(const CRegion &damage, const float alpha) {
  const auto FOCUS = Desktop::focusState()->monitor();
  const Vector2D renderOffset = FOCUS->m_position * FOCUS->m_scale;

  for (auto taskIt = renderTasks.rbegin(); taskIt != renderTasks.rend(); ++taskIt) {
    auto &task = *taskIt;
    CBox box = task.card->getPosition();

    box.x -= renderOffset.x;
    box.y -= renderOffset.y;

    task.card->draw(box, task.data.scale, std::min(task.data.alpha, alpha));
  }
}

void Monitor::activeChanged() {
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
  // Why am i doing this backwards??
  const auto target = (M_PI / 2) + (M_PI * 2.0f * activeWindow) / count;
  auto diff = target - rotation.target;
  diff = std::remainder(diff, 2.0f * M_PI);

  rotation.set(rotation.target + diff, false);
}

bool Monitor::isActive() const {
  LOG_SCOPE()
  return manager->activeMonitor == monitor->m_id;
}
