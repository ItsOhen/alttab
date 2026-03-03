#include "monitor.hpp"
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
void Monitor::update(float delta, const bool active = false) {
  LOG_SCOPE(Log::ERR)
  rotation.tick(delta, ROTATIONSPEED);
  zoom.set(active ? 1.0f : 0.0f, false);
  zoom.tick(delta, MONITORANIMATIONSPEED);
  alpha.set(active ? 1.0f : 0.0f, false);
  alpha.tick(delta, MONITORANIMATIONSPEED);
  bool damaged = (!rotation.done() || !zoom.done() || !alpha.done());

  const auto MONITOR = Desktop::focusState()->monitor();
  Vector2D cardSize = (MONITOR->m_size * MONITOR->m_scale) * WINDOWSIZE;
  int snapshotsThisFrame = 0;
  const int MAX_SNAPSHOTS_PER_FRAME = 3;
  for (auto &w : windows) {
    if (!w->ready && snapshotsThisFrame < MAX_SNAPSHOTS_PER_FRAME) {
      damaged |= w->snapshot(cardSize);
      snapshotsThisFrame++;
    }
  }
  animating = damaged;
}

void Monitor::draw(const CRegion &damage, const float &offset = 0.0f, const bool active = false) {
  LOG(ERR, "dim: {}, dimamount: {}, blur: {}", DIMENABLED, DIMAMOUNT, BLURBG);

  CRegion dmg;
  const int count = windows.size();
  if (count == 0)
    return;

  struct RenderTask {
    WindowCard *card;
    CardData data;
  };
  std::vector<RenderTask> tasks;

  // Alphablend the cards
  static float agressiveness = 1.0f;
  static float minAlpha = UNFOCUSEDALPHA * agressiveness;
  static float zoneWidth = 1.25f * agressiveness;
  for (int i = 0; i < count; ++i) {
    auto data = getCardBox(i, offset, active);
    float angle = ((2.0f * M_PI * i) / count) + rotation.current;
    float z = std::sin(angle);

    float normAngle = fmod(angle, 2.0f * M_PI);
    if (normAngle < 0)
      normAngle += 2.0f * M_PI;
    float dist = std::abs(normAngle - (M_PI / 2.0f));
    if (dist > M_PI)
      dist = (2.0f * M_PI) - dist;

    float alphaWeight = std::max(0.0f, (float)(1.0f - (dist / (M_PI / zoneWidth))));
    float smoothAlpha = std::pow(alphaWeight, 2.0f);

    float baseline = std::lerp(0.0f, minAlpha, alpha.current);
    float backAlpha = baseline + (z + 1.0f) * 0.2f;
    float localAlpha = std::lerp(backAlpha, 1.0f, smoothAlpha);
    float rowVisibility = std::lerp(0.5f, 1.0f, alpha.current);
    float finalAlpha = localAlpha * rowVisibility;

    float exaggeratedZ = z + (smoothAlpha * 0.1f);
    data.alpha = std::clamp(finalAlpha, 0.0f, 1.0f);
    data.z = exaggeratedZ;
    tasks.push_back({windows[i].get(), data});
  }
  std::sort(tasks.begin(), tasks.end(), [](const auto &a, const auto &b) {
    float weightA = a.data.z + (a.data.alpha * 0.01f);
    float weightB = b.data.z + (b.data.alpha * 0.01f);

    if (std::abs(weightA - weightB) < 0.0001f)
      return a.card < b.card;
    return weightA < weightB;
  });

  for (const auto &task : tasks) {
    CRegion cardDmg = task.data.box;
    task.card->draw(task.data.box, task.data.scale, task.data.alpha);
    dmg.add(cardDmg);
  }
#ifndef NDEBUG
  if (!dmg.empty())
    g_pHyprOpenGL->renderRect(dmg.getExtents(), {0.5, 0.5, 0.0, 0.5}, {});
#endif
  if (animating) {
    if (POWERSAVE) {
      CRegion totalDamage = dmg;
      totalDamage.add(lastDamage);
      monitor->addDamage(totalDamage);
      lastDamage = dmg;
    } else {
      g_pHyprRenderer->damageMonitor(monitor);
    }
  }
}

Monitor::CardData Monitor::getCardBox(int index, const float &offset = 0.0f, const bool active = false) {
  if (index < 0 || index >= (int)windows.size())
    return {};

  const auto w = windows[index]->window;
  if (!w)
    return {};

  auto surface = w->wlSurface()->getSurfaceBoxGlobal().value_or(CBox{0, 0, 0, 0}).size();
  float aspect = (float)surface.x / std::max(1.0f, (float)surface.y);
  if (aspect <= 0)
    aspect = 1.77f;

  // Creates weird offsets if not using the currently active monitor
  const auto MONITOR = Desktop::focusState()->monitor();
  const auto mSize = MONITOR->m_size * MONITOR->m_scale;

  const float maxHeight = mSize.y * WINDOWSIZE;
  const float maxWidth = mSize.x * (WINDOWSIZE * 1.5);

  Vector2D baseSize;
  baseSize.y = maxHeight;
  baseSize.x = baseSize.y * aspect;

  if (baseSize.x > maxWidth) {
    baseSize.x = maxWidth;
    baseSize.y = baseSize.x / aspect;
  }

  const Vector2D center = {(mSize.x / 2.0f), (mSize.y / 2.0f) + offset};
  const int count = windows.size();
  // size of the spinnyboi
  const float radius = (MONITOR->m_size.x * 0.5f) * CAROUSELSIZE;
  const float stretchX = 1.4f;

  // viewport position
  float baseAngle = ((2.0f * M_PI * index) / count) + rotation.current;
  float warpScale = WARP + (1.0f - WINDOWSIZEINACTIVE) * 0.2f;
  float angle = baseAngle - warpScale * std::sin(2.0f * baseAngle);
  float z = std::sin(angle);

  // clamp to [0, 2PI] so we don't spin out of control (even though it looks very funny)
  float normAngle = fmod(angle, 2.0f * M_PI);
  if (normAngle < 0)
    normAngle += 2.0f * M_PI;

  float dist = std::abs(normAngle - (M_PI / 2.0f));
  if (dist > M_PI)
    dist = (2.0f * M_PI) - dist;

  // scale focused so it stands out abit
  float focusWeight = std::max(0.0, (double)(1.0f - (dist / (M_PI / 2.0f))));
  float focusFactor = std::pow(focusWeight, 2.5);
  float targetZoom = std::lerp(1.0f, WINDOWSIZEACTIVE, focusFactor);
  float depthFalloff = std::lerp(WINDOWSIZEINACTIVE, 1.0f, (z + 1.0f) / 2.0f);
  float actualZoom = std::lerp(1.0f, targetZoom, zoom.current);

  float s = depthFalloff * actualZoom;
  s = std::max(s, 0.01f);
  Vector2D size = baseSize * s;

  float radiusScale = radius * std::lerp(0.85f, 1.0f, (z + 1.0f) / 2.0f);
  float tiltRadian = TILT * (M_PI / 180.0f);
  float tiltOffset = radius * std::sin(tiltRadian);

  Vector2D pos;
  pos.x = center.x + (radiusScale * stretchX) * std::cos(angle) - (size.x / 2.0f);
  pos.y = (center.y - tiltOffset) - (z * -tiltOffset) - (size.y / 2.0f);

  return {.box = CBox{pos, size}, .scale = s};
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
