#include "manager.hpp"
#include <hyprutils/math/Vector2D.hpp>
#include <src/Compositor.hpp>
#include <src/desktop/state/FocusState.hpp>
#include <src/helpers/Color.hpp>
#include <src/helpers/Monitor.hpp>
#include <src/plugins/PluginAPI.hpp>
#include <src/render/pass/RectPassElement.hpp>
#include <src/render/pass/TexPassElement.hpp>
#define private public
#include <src/render/OpenGL.hpp>
#include <src/render/Renderer.hpp>
#undef private

static const auto POWERSAVE = false;

Monitor::Monitor(PHLMONITOR monitor) : monitor(monitor) {
  createTexture();
  activeWindow = 0;
  rotation.snap(M_PI / 2.0f);
  animating = false;
}
void Monitor::createTexture() {
  g_pHyprRenderer->makeEGLCurrent();

  if (!bgFb.isAllocated() || bgFb.m_size != monitor->m_size)
    bgFb.alloc(monitor->m_size.x, monitor->m_size.y, monitor->m_drmFormat);
  CRegion fullRegion = CBox({0, 0}, monitor->m_size);

  g_pHyprRenderer->beginRender(monitor, fullRegion, RENDER_MODE_FULL_FAKE, nullptr, &bgFb, false);
  g_pHyprRenderer->renderWorkspace(monitor, monitor->m_activeWorkspace, NOW, monitor->logicalBox());
  g_pHyprRenderer->m_renderPass.render(fullRegion);
  g_pHyprRenderer->m_renderPass.clear();
  g_pHyprRenderer->endRender();
  texture = bgFb.getTexture();

  if (!blurFb.isAllocated() || blurFb.m_size != monitor->m_size / 2)
    blurFb.alloc(monitor->m_size.x / 2, monitor->m_size.y / 2, monitor->m_drmFormat);
  CRegion blurRegion = CBox({0, 0}, monitor->m_size);

  g_pHyprRenderer->beginRender(monitor, blurRegion, RENDER_MODE_FULL_FAKE, nullptr, &blurFb, false);

  CBox destBox = {{0, 0}, monitor->m_size / 2};
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
  const auto DIMENABLED = *CConfigValue<Hyprlang::INT>("plugin:alttab:dim");
  const auto DIMAMOUNT = *CConfigValue<Hyprlang::FLOAT>("plugin:alttab:dim_amount");
  const auto BLURBG = *CConfigValue<Hyprlang::INT>("plugin:alttab:blur");
  auto dmg = damage;
  if (BLURBG)
    g_pHyprOpenGL->renderTexture(blurred, monitor->logicalBox(), {.damage = &dmg});
  else if (POWERSAVE)
    g_pHyprOpenGL->renderTexture(texture, monitor->logicalBox(), {.damage = &dmg});
  if (DIMENABLED)
    g_pHyprOpenGL->renderRect(dmg.getExtents(), {0, 0, 0, DIMAMOUNT}, {});
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
  windows[activeWindow]->isActive = false;
  select(activeWindow + 1);
  windows[activeWindow]->isActive = true;
}
void Monitor::prev() {
  windows[activeWindow]->isActive = false;
  select(activeWindow - 1);
  windows[activeWindow]->isActive = true;
}
void Monitor::update(float delta) {
  // LOG_SCOPE(Log::ERR)
  const auto ROTATIONSPEED = *CConfigValue<Hyprlang::FLOAT>("plugin:alttab:animation_speed");
  rotation.tick(delta, ROTATIONSPEED);
  bool damaged = rotation.done();

  static Vector2D cardSize = monitor->m_size * 0.4f;
  auto since = std::chrono::duration_cast<std::chrono::milliseconds>(NOW - lastFrame).count();
  const auto refresh = [&](int idx, auto &w) {
    w->needsRefresh = false;
    w->snapshot(cardSize);
    g_pHyprRenderer->damageBox(getCardBox(idx).box);
    damaged = true;
  };

  for (auto i = 0; i < windows.size(); ++i) {
    auto &w = windows[i];
    const auto since = std::chrono::duration_cast<std::chrono::milliseconds>(NOW - w->lastCommit).count();
    const auto th = activeWindow == w ? 1000 / 60 : 1000 / 30;
    if (since >= th && w->needsRefresh) {
      refresh(i, w);
    }
  }
}

void Monitor::draw(const CRegion &damage) {
  const auto DIMENABLED = *CConfigValue<Hyprlang::INT>("plugin:alttab:dim");
  const auto DIMAMOUNT = *CConfigValue<Hyprlang::FLOAT>("plugin:alttab:dim_amount");
  const auto BLURBG = *CConfigValue<Hyprlang::INT>("plugin:alttab:blur");
  const auto UNFOCUSEDALPHA = *CConfigValue<Hyprlang::FLOAT>("plugin:alttab:unfocused_alpha");

  LOG(ERR, "dim: {}, dimamount: {}, blur: {}", DIMENABLED, DIMAMOUNT, BLURBG);

  auto dmg = damage;
  if (!POWERSAVE) {
    g_pHyprOpenGL->renderRect(dmg.getExtents(), CHyprColor(0.0, 0.0, 0.0, (DIMENABLED) ? DIMAMOUNT : 0), {.blur = sc<bool>(BLURBG)});
  } else {
    renderTexture(damage);
  }

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
    auto data = getCardBox(i);
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

    float backAlpha = minAlpha + (z + 1.0f) * 0.15f;
    float alpha = std::lerp(backAlpha, 1.0f, smoothAlpha);

    float exaggeratedZ = z + (smoothAlpha * 0.1f);
    data.alpha = alpha;
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
    // g_pHyprRenderer->damageBox(task.box);
    task.card->draw(task.data.box, task.data.alpha, task.data.scale);
    if (POWERSAVE)
      g_pHyprRenderer->damageBox(task.data.box);
    else
      g_pHyprRenderer->damageMonitor(monitor);
  }
}

Monitor::CardData Monitor::getCardBox(int index) {
  if (index < 0 || index >= (int)windows.size())
    return {};

  const auto w = windows[index]->window;
  if (!w)
    return {};

  auto surface = w->wlSurface()->getSurfaceBoxGlobal().value_or(CBox{0, 0, 0, 0}).size();
  float aspect = (float)surface.x / std::max(1.0f, (float)surface.y);
  if (aspect <= 0)
    aspect = 1.77f;

  const float maxHeight = monitor->m_size.y * 0.25f;
  const float maxWidth = monitor->m_size.x * 0.35f;

  Vector2D baseSize;
  baseSize.y = maxHeight;
  baseSize.x = baseSize.y * aspect;

  if (baseSize.x > maxWidth) {
    baseSize.x = maxWidth;
    baseSize.y = baseSize.x / aspect;
  }

  const Vector2D center = monitor->m_size / 2.0f;
  const int count = windows.size();
  // size of the spinnyboi
  float dynamicRadius = (count * 400.0f) / (2.0f * M_PI);
  const float radius = std::clamp(dynamicRadius, 400.0f, (float)monitor->m_size.x * 0.95f);
  const float tiltOffset = -75.0f;

  // viewport position
  float baseAngle = ((2.0f * M_PI * index) / count) + rotation.current;
  float warpedAngle = baseAngle - 0.3f * std::sin(2.0f * baseAngle);
  float angle = warpedAngle;
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
  float focusBonus = std::pow(focusWeight, 2.5) * 0.3f;

  float s = 0.7f + (z + 1.0f) * 0.15f + focusBonus;

  s = std::max(s, 0.01f);

  Vector2D size = baseSize * s;
  Vector2D pos;
  const float stretchX = 1.4f;
  pos.x = center.x + (radius * stretchX) * std::cos(angle) - (size.x / 2.0f);
  // bring the damn thing back to center.. Stupid tilt getting me everytime..
  pos.y = center.y - (z * tiltOffset) - (size.y / 2.0f) - ((focusBonus * baseSize.y) * 0.5f);

  return {.box = CBox{pos + monitor->m_position, size}, .scale = s};
}

void Monitor::select(int index) {
  const int count = windows.size();
  if (count <= 0)
    return;

  activeWindow = (index + count) % count;

  float angle = (M_PI / 2.0f) - ((2.0f * M_PI * activeWindow) / count);
  float diff = angle - rotation.target;

  while (diff > M_PI)
    diff -= 2.0f * M_PI;
  while (diff < -M_PI)
    diff += 2.0f * M_PI;

  rotation.set(rotation.target + diff, false);

  animating = true;
}

Manager::Manager() {
  LOG_SCOPE()
  listeners.config = HOOK_EVENT(config.reloaded, [this]() {
    onConfigReload();
  });
  listeners.windowCreated = HOOK_EVENT(window.open, [this](auto w) {
    onWindowCreated(w);
  });
  listeners.windowDestroyed = HOOK_EVENT(window.close, [this](auto w) {
    onWindowDestroyed(w);
  });
  listeners.render = HOOK_EVENT(render.stage, [this](auto s) {
    onRender(s);
  });

  lastFrame = NOW;
}

void Manager::activate() {
  LOG_SCOPE()
  active = true;
  activeMonitor = Desktop::focusState()->monitor()->m_id;
  rebuild();
}

void Manager::deactivate() {
  LOG_SCOPE()
  active = false;
}

void Manager::toggle() {
  LOG_SCOPE()
  LOG(ERR, "toggle");
  active = !active;
  if (active)
    activate();
  else
    deactivate();
}

void Manager::confirm() {
  Desktop::focusState()->fullWindowFocus(monitors[activeMonitor]->windows[monitors[activeMonitor]->activeWindow]->window, Desktop::FOCUS_REASON_KEYBIND);
  deactivate();
}

void Manager::update(float delta) {
  LOG_SCOPE()
  for (const auto &[id, m] : monitors) {
    m->update(delta);
  }
}

void Manager::up() {
  activeMonitor = (activeMonitor - 1 + monitors.size()) % monitors.size();
}

void Manager::down() {
  activeMonitor = (activeMonitor + 1) % monitors.size();
}

void Manager::next() {
  monitors[activeMonitor]->next();
}

void Manager::prev() {
  monitors[activeMonitor]->prev();
}

void Manager::draw(const CRegion &damage) {
  LOG_SCOPE()
  for (const auto &[id, m] : monitors) {
    m->draw(damage);
  }
}

void Manager::onConfigReload() {
  ;
}

void Manager::onWindowCreated(PHLWINDOW window) {
  ;
}

void Manager::onWindowDestroyed(PHLWINDOW window) {
  if (!window)
    return;

  auto mon = window->m_monitor.lock();

  if (mon && monitors.contains(mon->m_id)) {
    if (monitors[mon->m_id]->removeWindow(window) == 0) {
      monitors.erase(mon->m_id);
    }
  } else {
    for (auto &[id, mon] : monitors) {
      if (mon->removeWindow(window) == 0) {
        monitors.erase(id);
        break;
      }
    }
  }
}

void Manager::onRender(eRenderStage stage) {
  if (!active)
    return;

  switch (stage) {
  case eRenderStage::RENDER_PRE:
    update(FloatTime(NOW - lastFrame).count());
    lastFrame = NOW;
    break;
  case eRenderStage::RENDER_LAST_MOMENT:
    g_pHyprRenderer->m_renderPass.add(makeUnique<RenderPass>());
    break;
  default:
    break;
  }
}

void Manager::rebuild() {
  LOG_SCOPE()

  const auto INCLUDESPECIAL = *CConfigValue<Hyprlang::INT>("plugin:alttab:include_special");

  for (const auto &m : g_pCompositor->m_monitors) {
    if (!m->m_enabled)
      continue;
    monitors[m->m_id] = makeUnique<Monitor>(m);
    g_pCompositor->scheduleFrameForMonitor(m);
  }

  auto activeWindow = Desktop::focusState()->window();

  for (auto &[monID, pMonitor] : monitors) {
    std::vector<PHLWINDOW> monitorWindows;
    for (const auto &w : g_pCompositor->m_windows) {
      if (!INCLUDESPECIAL && w->m_workspace && w->m_workspace->m_isSpecialWorkspace)
        continue;
      if (w->m_isMapped && w->m_monitor.lock() && w->m_monitor.lock()->m_id == monID)
        monitorWindows.emplace_back(w);
    }

    for (const auto &w : monitorWindows) {
      auto card = pMonitor->addWindow(w);
      if (w == activeWindow) {
        pMonitor->activeWindow = pMonitor->windows.size() - 1;
        const int count = monitorWindows.size();
        const float angle = (M_PI / 2.0f) - ((2.0f * M_PI * pMonitor->activeWindow) / count);
        pMonitor->rotation.snap(angle);
      }
      if (w->wlSurface() && w->wlSurface()->resource()) {
        w->wlSurface()->resource()->frame(NOW);
      }
    }
  }
}
void RenderPass::draw(const CRegion &damage) {
  manager->draw(damage);
}
