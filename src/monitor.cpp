#include "monitor.hpp"
#include "defines.hpp"
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
  if (BLURBG)
    g_pHyprOpenGL->renderTexture(blurred, box, {});
  else if (POWERSAVE)
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
void Monitor::next() {
  select(activeWindow + 1);
}
void Monitor::prev() {
  select(activeWindow - 1);
}

bool Monitor::animate(const float delta, const bool active) {
  zoom.set(active ? 1.0f : 0.0f, false);
  alpha.set(active ? 1.0f : 0.0f, false);
  rotation.tick(delta, ROTATIONSPEED);
  zoom.tick(delta, MONITORANIMATIONSPEED);
  alpha.tick(delta, MONITORANIMATIONSPEED);
  return !rotation.done() || !zoom.done() || !alpha.done();
}

void Monitor::update(const float delta, const bool active) {
  const auto MONITOR = Desktop::focusState()->monitor();
  const Vector2D mSize = MONITOR->m_size * MONITOR->m_scale;

  bool damage = animate(delta, active);

  renderTasks.clear();
  for (int i = 0; i < windows.size(); ++i) {
    auto data = getCardBox(i, 0.0f);

    const auto padding = 4;
    const auto barHeight = (FONTSIZE + padding) * data.scale;
    double pX1 = data.box.x, pY1 = data.box.y + barHeight;
    double pW = data.box.width, pH = data.box.height - barHeight;

    double interW = std::max(0.0, std::min(mSize.x, pX1 + pW) - std::max(0.0, pX1));
    double interH = std::max(0.0, std::min(mSize.y, pY1 + pH) - std::max(0.0, pY1));
    float visibility = (pW * pH > 0) ? (float)((interW * interH) / (pW * pH)) : 0.0f;

    renderTasks.emplace_back(RenderTask{windows[i].get(), data, visibility, FloatTime(NOW - windows[i]->lastSnapshot).count()});
  }

  std::sort(renderTasks.begin(), renderTasks.end(), [](const RenderTask &a, const RenderTask &b) {
    return a.since > b.since;
  });

  int snapshotsDone = 0;

  for (auto &task : renderTasks) {
    // Only update if it's actually visible enough to care
    if (task.visibility > 0.10f) {
      if (!task.card->ready || task.since > 0.5f) {
        if (snapshotsDone < 2) {
          task.card->requestFrame(MONITOR);
          task.card->snapshot(mSize * WINDOWSIZE);
          snapshotsDone++;
          damage = true;
        }
      }
    }
  }

  animating = damage;
}

void Monitor::draw(const CRegion &damage, const float &offset, const bool active) {
  if (renderTasks.empty())
    return;

  std::sort(renderTasks.begin(), renderTasks.end(), [](const auto &a, const auto &b) {
    return a.data.z < b.data.z;
  });

  CRegion dmg;
  for (const auto &task : renderTasks) {
    auto box = task.data.box;
    box.translate({0.0f, offset});
    task.card->draw(box, task.data.scale, task.data.alpha);
    dmg.add(box);
  }
#ifndef NDEBUG
  if (!dmg.empty())
    g_pHyprOpenGL->renderRect(dmg.getExtents(), {0.5, 0.5, 0.0, 0.5}, {});
#endif
}

Monitor::CardData Monitor::getCardBox(int index, const float &offset) {
  const int count = windows.size();
  if (index < 0 || index >= count)
    return {};

  const auto MONITOR = Desktop::focusState()->monitor();
  const auto mSize = MONITOR->m_size * MONITOR->m_scale;
  const Vector2D center = {mSize.x / 2.0f, (mSize.y / 2.0f) + offset};

  float baseAngle = ((2.0f * M_PI * index) / count) + rotation.current;
  float warpScale = WARP + (1.0f - WINDOWSIZEINACTIVE) * 0.2f;
  float angle = baseAngle - warpScale * std::sin(2.0f * baseAngle);

  float normAngle = fmod(angle, 2.0f * M_PI);
  if (normAngle < 0)
    normAngle += 2.0f * M_PI;
  float dist = std::abs(normAngle - (M_PI / 2.0f));
  if (dist > M_PI)
    dist = (2.0f * M_PI) - dist;

  float z = std::sin(angle);
  float focusWeight = std::pow(std::max(0.0f, (float)(1.0f - (dist / (M_PI / 2.0f)))), 2.5f);
  float depthScale = std::lerp(WINDOWSIZEINACTIVE, 1.0f, (z + 1.0f) / 2.0f);
  float scale = depthScale * std::lerp(1.0f, WINDOWSIZEACTIVE, focusWeight * zoom.current);

  auto surfaceSize = windows[index]->window->wlSurface()->getSurfaceBoxGlobal().value_or(CBox{0, 0, 0, 0}).size();
  float aspect = (surfaceSize.y > 0) ? (float)surfaceSize.x / surfaceSize.y : 1.77f;

  Vector2D size = {mSize.y * WINDOWSIZE * aspect * scale, mSize.y * WINDOWSIZE * scale};
  if (size.x > mSize.x * WINDOWSIZE * 1.5f) {
    size.x = mSize.x * WINDOWSIZE * 1.5f;
    size.y = size.x / aspect;
  }

  float radius = (mSize.x * 0.5f) * CAROUSELSIZE;
  float radiusScale = radius * std::lerp(0.85f, 1.0f, (z + 1.0f) / 2.0f);
  float tiltOffset = radius * std::sin(TILT * (M_PI / 180.0f));

  Vector2D pos = {
      center.x + (radiusScale * 1.4f) * std::cos(angle) - (size.x / 2.0f),
      (center.y - tiltOffset) - (z * -tiltOffset) - (size.y / 2.0f)};

  float alphaWeight = std::pow(std::max(0.0f, (float)(1.0f - (dist / (M_PI / 1.25f)))), 2.0f);
  float baseline = std::lerp(0.0f, UNFOCUSEDALPHA, alpha.current);
  float finalAlpha = std::lerp(baseline + (z + 1.0f) * 0.2f, 1.0f, alphaWeight) * std::lerp(0.5f, 1.0f, alpha.current);

  CBox box{pos, size};
  bool isVisible = finalAlpha > 0.01f && box.overlaps({0, 0, mSize.x, mSize.y});

  return {box, scale, std::clamp(finalAlpha, 0.0f, 1.0f), z + (alphaWeight * 0.1f), isVisible};
}

PHLWINDOW Monitor::select(int index) {
  const int count = windows.size();
  if (count <= 0)
    return nullptr;

  // very ugly..
  windows[activeWindow]->isActive = false;
  activeWindow = (index + count) % count;
  windows[activeWindow]->isActive = true;

  float angle = (M_PI / 2.0f) - ((2.0f * M_PI * activeWindow) / count);
  float diff = angle - rotation.target;

  while (diff > M_PI)
    diff -= 2.0f * M_PI;
  while (diff < -M_PI)
    diff += 2.0f * M_PI;

  rotation.set(rotation.target + diff, false);
  return windows[activeWindow]->window;
}
