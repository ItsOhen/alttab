#include "manager.hpp"
#include "defines.hpp"
#include "helpers.hpp"
#include "logger.hpp"
#include <aquamarine/output/Output.hpp>
#include <chrono>
#include <hyprutils/math/Vector2D.hpp>
#include <src/Compositor.hpp>
#include <src/desktop/history/WindowHistoryTracker.hpp>
#include <src/desktop/state/FocusState.hpp>
#include <src/helpers/Color.hpp>
#include <src/helpers/Monitor.hpp>
#include <src/managers/PointerManager.hpp>
#include <src/managers/eventLoop/EventLoopManager.hpp>
#include <src/managers/input/InputManager.hpp>
#include <src/plugins/PluginAPI.hpp>
#include <src/protocols/PresentationTime.hpp>

#include <src/render/pass/TexPassElement.hpp>
#define protected public
#include <src/render/OpenGL.hpp>
#include <src/render/Renderer.hpp>
#undef private

#ifndef NDEBUG
static int counter = 0;
static int lastCounter = 0;
#endif

using namespace alttab;

Manager::Manager() : monitorOffset(&Config::monitorAnimationSpeed),
                     monitorFade(&Config::monitorFade) {
  LOG_SCOPE()

#ifdef HYPRLAND_LEGACY
  listeners.config = HyprlandAPI::registerCallbackDynamic(PHANDLE, "configReloaded", [this](void *self, SCallbackInfo &info, std::any data) { onConfigReload(); });
  listeners.windowCreated = HyprlandAPI::registerCallbackDynamic(PHANDLE, "openWindow", [this](void *self, SCallbackInfo &info, std::any data) { onWindowCreated(std::any_cast<PHLWINDOW>(data)); });
  listeners.windowDestroyed = HyprlandAPI::registerCallbackDynamic(PHANDLE, "closeWindow", [this](void *self, SCallbackInfo &info, std::any data) { onWindowDestroyed(std::any_cast<PHLWINDOW>(data)); });
  listeners.render = HyprlandAPI::registerCallbackDynamic(PHANDLE, "render", [this](void *self, SCallbackInfo &info, std::any data) { onRender(std::any_cast<eRenderStage>(data)); });
  listeners.focusChange = HyprlandAPI::registerCallbackDynamic(PHANDLE, "monitorFocusChange", [this](void *self, SCallbackInfo &info, std::any data) { onFocusChange(std::any_cast<PHLMONITOR>(data)); });
  listeners.monitorAdded = HyprlandAPI::registerCallbackDynamic(PHANDLE, "monitorAdded", [this](void *self, SCallbackInfo &info, std::any data) { rebuild(); });
  listeners.monitorRemoved = HyprlandAPI::registerCallbackDynamic(PHANDLE, "monitorRemoved", [this](void *self, SCallbackInfo &info, std::any data) { rebuild(); });
#else
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
  listeners.focusChange = HOOK_EVENT(monitor.focused, [this](auto m) {
    onFocusChange(m);
  });
  listeners.monitorAdded = HOOK_EVENT(monitor.added, [this](auto m) {
    rebuild();
  });
  listeners.monitorRemoved = HOOK_EVENT(monitor.removed, [this](auto m) {
    rebuild();
  });
  listeners.mouseClick = HOOK_EVENT(input.mouse.button, [this](auto button, auto &cbInfo) {
    // cbInfo is only for .cancelled
    if (this->active) {
      cbInfo.cancelled = true;
      onMouseClick(button);
    }
  });
  listeners.mouseMove = HOOK_EVENT(input.mouse.move, [this](auto pos, auto &cbInfo) {
    // if (this->active) {
    //   cbInfo.cancelled = true;
    // }
    // Noop until i figure out a better way to handle cancel, so that it still does monitor change and cursor draws.
    ;
  });
#endif

  lastFrame = lastUpdate = NOW;
}

void Manager::damageMonitors() {
  for (auto &[id, mon] : monitors) {
    g_pHyprRenderer->damageMonitor(mon->monitor);
  }
}

void Manager::activate() {
  LOG_SCOPE()
  active = true;
  graceTimer = makeShared<CEventLoopTimer>(std::chrono::milliseconds(Config::grace), [this](SP<CEventLoopTimer> timer, void *data) { this->init(); }, nullptr);
  g_pEventLoopManager->addTimer(graceTimer);
}

bool Manager::setLayout() {
  std::string styleName = toLower(Config::style);

  if (styleName == "grid") {
    layoutStyle = makeShared<Grid>();
  } else if (styleName == "carousel") {
    layoutStyle = makeShared<Carousel>();
  } else if (styleName == "slide") {
    layoutStyle = makeShared<Slide>();
  } else {
    layoutStyle = makeShared<Carousel>();
  }
  return (layoutStyle != nullptr);
}

void Manager::init() {
  graceExpired = true;
  activeMonitor = Desktop::focusState()->monitor()->m_id;
  monitorFade.set(1.0f, false);
  stack.clear();
  rebuild();
  lastFrame = NOW;
  OVERRIDE_WORKSPACE = true;
  damageMonitors();
}

void Manager::deactivate() {
  LOG_SCOPE()
  active = false;
  graceTimer->cancel();
  for (const auto &[id, mon] : monitors) {
    g_pHyprRenderer->damageMonitor(mon->monitor);
  }
  graceTimer.reset();
  stack.clear();
  monitors.clear();
  OVERRIDE_WORKSPACE = false;
}

void Manager::toggle() {
  LOG_SCOPE()
  active = !active;
  if (active)
    activate();
  else
    deactivate();
}

void Manager::confirm() {
  auto getFallbackWindow = [&]() -> PHLWINDOWREF {
    const auto history = Desktop::History::windowTracker()->fullHistory();
    if (history.size() >= 2)
      return *(history | std::views::reverse | std::views::drop(1)).begin();
    return Desktop::focusState()->window();
  };

  PHLWINDOWREF selected;
  bool hasMonitor = monitors.contains(activeMonitor);

  if (!hasMonitor || !monitors[activeMonitor] || monitors[activeMonitor]->windows.empty()) {
    selected = getFallbackWindow();
  } else if (graceExpired) {
    const auto &mon = monitors[activeMonitor];
    selected = mon->windows[mon->activeWindow]->window;
  } else {
    selected = getFallbackWindow();
  }

  if (selected) {
    if (Config::bringToActive && selected->m_workspace)
      g_pKeybindManager->m_dispatchers["focusworkspaceoncurrentmonitor"](selected->m_workspace->m_name);

#ifdef HYPRLAND_LEGACY
    Desktop::focusState()->fullWindowFocus(selected.lock());
    g_pCompositor->changeWindowZOrder(selected.lock(), true);
#else
    Desktop::focusState()->fullWindowFocus(selected.lock(), Desktop::FOCUS_REASON_KEYBIND);
    g_pCompositor->changeWindowZOrder(selected.lock(), true);
#endif
  }

  deactivate();
}

void Manager::update(float delta) {
  LOG_SCOPE(Log::UPDATE)
  const auto MONITOR = Desktop::focusState()->monitor();
  const Vector2D monitorPos = MONITOR->m_position;
  const bool animating = AnimationManager::get().tick(delta) || stack.empty();
  const float spacing = MONITOR->m_size.y * Config::monitorSpacing;
  stack.clear();
  CRegion damage;
  int i = 0;
  for (auto &[id, mon] : monitors) {
    CRegion mDamage;
    float off = (i - monitorOffset.current) * spacing;
    mon->position = {monitorPos.x, monitorPos.y + off, MONITOR->m_pixelSize.x, MONITOR->m_pixelSize.y};
    float z = (id == activeMonitor) ? 1000.0f : -std::abs(i - monitorOffset.current);
    if (animating)
      mon->update(delta, off, mDamage);
    damage.add(mDamage);
    i++;
    stack.push_back({.monitor = mon.get(), .offset = off, .z = z});
  }
  std::sort(stack.begin(), stack.end(), [](const auto &a, const auto &b) {
    return a.z < b.z;
  });
  lastFrame = NOW;
  CRegion total = damage;
  total.add(previousFrameDamage);
  g_pHyprRenderer->damageRegion(total);
  if (animating)
    previousFrameDamage = damage;
}

void Manager::move(Direction dir) {
  LOG_SCOPE(Log::MOVE)

  auto it = monitors.find(activeMonitor);
  if (it == monitors.end()) {
    if (monitors.empty())
      return;
    it = monitors.begin();
  }

  auto &mon = it->second;
  const auto res = layoutStyle->onMove(dir, mon->activeWindow, mon->windows.size());

  if (res.index.has_value() && !mon->windows.empty()) {
    mon->activeWindow = res.index.value();
    mon->activeChanged();
  } else if (res.changeMonitor) {
    if (monitors.size() < 2)
      return;

    if (dir == Direction::DOWN || dir == Direction::RIGHT) {
      it++;
      if (it == monitors.end())
        it = monitors.begin();
    } else {
      if (it == monitors.begin())
        it = std::prev(monitors.end());
      else
        it--;
    }

    activeMonitor = it->first;
    monitorOffset.set(activeMonitor);
  }
}

void Manager::draw(MONITORID monid, const CRegion &damage) {
  ;
}

void Manager::renderBackground(MONITORID monid, const CRegion &damage) {
  if (!monitors.contains(monid))
    return;
  auto &mon = monitors[monid];
  if (!mon->texture)
    return;

  auto tex = (Config::blurBG) ? mon->blurred : mon->texture;
  const auto box = CBox{{}, mon->monitor->m_pixelSize};
  CTexPassElement::SRenderData data;
  data.tex = tex;
  data.box = box;

  data.a = 1.0f;
  data.damage = {};
  g_pHyprRenderer->m_renderPass.add(makeUnique<CTexPassElement>(data));
  g_pHyprRenderer->m_renderPass.render(damage);
  g_pHyprRenderer->m_renderPass.clear();

  if (Config::dimEnabled) {
    g_pHyprOpenGL->renderRect(box, {0.0, 0.0, 0.0, Config::dimAmount}, {});
  }
}

void Manager::renderMonitors(const CRegion &damage) {
  LOG_SCOPE(Log::DRAW)
  LOG(Log::DRAW, "stack size: {}", stack.size());
  for (auto &el : stack) {
    el.monitor->draw(damage, monitorFade.current);
  }
}

void Manager::onConfigReload() {
  auto getConf = [&](const std::string &name) -> Hyprlang::CConfigValue * {
    return HyprlandAPI::getConfigValue(PHANDLE, "plugin:alttab:" + name);
  };

#define X(type, name, conf, def)                                     \
  {                                                                  \
    auto val = getConf(conf);                                        \
    if (val)                                                         \
      Config::name = std::any_cast<Hyprlang::type>(val->getValue()); \
  }
  CONFIG_VARS
#undef X

#define X(type, name, conf)                                                \
  {                                                                        \
    auto val = getConf(conf);                                              \
    if (val)                                                               \
      Config::name.get() = std::any_cast<Hyprlang::type>(val->getValue()); \
  }
  CONFIG_VARS_OPTIONAL_FLOAT
#undef X

  auto getGradient = [&](const std::string &name) -> CGradientValueData * {
    auto val = HyprlandAPI::getConfigValue(PHANDLE, name);
    if (!val || !val->getValue().has_value())
      return nullptr;
    try {
      return sc<CGradientValueData *>(std::any_cast<void *>(val->getValue()));
    } catch (...) {
      return nullptr;
    }
  };

  Config::activeBorderColor = getGradient("plugin:alttab:border_active");
  Config::inactiveBorderColor = getGradient("plugin:alttab:border_inactive");

  stack.clear();
}

void Manager::onWindowCreated(PHLWINDOW window) {
  // TODO: add window to specific monitor
  if (!active)
    return;

  rebuild();
}

void Manager::onWindowDestroyed(PHLWINDOW window) {
  if (!window || !active)
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

  const auto FOCUSED_MON = Desktop::focusState()->monitor();

  switch (stage) {
  case eRenderStage::RENDER_PRE: {
    auto delta = FloatTime(NOW - lastUpdate).count();
    LOG(Log::DAMAGE, "previousFrameDamage: x1={}, y1={}, x2={}, y2={}", previousFrameDamage.getExtents().x, previousFrameDamage.getExtents().y, previousFrameDamage.getExtents().w, previousFrameDamage.getExtents().h);
    update(delta);
    LOG(Log::DAMAGE, "previousFrameDamage: x1={}, y1={}, x2={}, y2={}", previousFrameDamage.getExtents().x, previousFrameDamage.getExtents().y, previousFrameDamage.getExtents().w, previousFrameDamage.getExtents().h);
    lastUpdate = NOW;
  } break;

  case eRenderStage::RENDER_LAST_MOMENT: {
    const auto& rd = g_pHyprOpenGL->m_renderData;
    const PHLMONITOR MONITOR = rd.pMonitor.lock();
    if (!MONITOR)
      return;
    if (!monitors.contains(MONITOR->m_id))
      return;
    CRegion damage = rd.damage; // mutable copy — renderSoftwareCursorsFor requires non-const ref
    renderBackground(rd.pMonitor->m_id, damage);
    if (!Config::splitMonitor)
      monitors[MONITOR->m_id]->draw(damage, monitorFade.current);
    else if (MONITOR == FOCUSED_MON) {
      LOG(Log::DRAW, "Rendering Monitors");
      renderMonitors(damage);
    }
#ifndef NDEBUG
    renderDamage(damage);
    // Overlay->add(std::format("ActiveID: {}, Offset: {:.2f}", activeMonitor, monitorOffset.current));
    // Overlay->draw(MONITOR);
#endif

    // stupid cursor..
    g_pPointerManager->renderSoftwareCursorsFor(rd.pMonitor.lock(), Time::steadyNow(), damage);

    if (MONITOR == FOCUSED_MON)
      g_pCompositor->scheduleFrameForMonitor(MONITOR);
  } break;

  default:
    break;
  }
}

void Manager::onFocusChange(PHLMONITOR monitor) {
  if (monitor == nullptr)
    return;
  activeMonitor = monitor->m_id;
  monitorOffset.set(activeMonitor);
}

// Feels like indexing by size_t id's might have been a mistake at this point..
void Manager::onMouseClick(const IPointer::SButtonEvent button) {
  LOG_SCOPE(Log::MOUSE)
  if (button.button != BTN_LEFT || button.state != WL_POINTER_BUTTON_STATE_PRESSED)
    return;

  const auto mousePos = g_pInputManager->getMouseCoordsInternal();
  const auto MONITOR = Desktop::focusState()->monitor();

  const Vector2D localMouse = (mousePos - MONITOR->m_position);

  for (auto it = stack.begin(); it != stack.end(); ++it) {
    auto *mon = it->monitor;

    for (auto taskIt = mon->renderTasks.begin(); taskIt != mon->renderTasks.end(); ++taskIt) {
      auto &task = *taskIt;

      if (task.card->getPosition().containsPoint(localMouse)) {
        const auto id = mon->monitor->m_id;
        LOG(Log::MOUSE, "HIT! {} on Monitor ID: {}", task.card->window->m_title, id);
        activeMonitor = id;
        auto mapIt = monitors.find(id);
        if (mapIt != monitors.end()) {
          size_t idx = std::distance(monitors.begin(), mapIt);
          monitorOffset.set((float)idx);
        }

        auto winIt = std::find_if(mon->windows.begin(), mon->windows.end(),
                                  [&](const auto &wp) { return wp.get() == task.card; });

        if (winIt != mon->windows.end()) {
          mon->activeWindow = std::distance(mon->windows.begin(), winIt);
          mon->activeChanged();
        }
        return;
      }
    }
  }
}

void Manager::rebuild() {
  LOG_SCOPE()
  if (!active)
    return;
  setLayout();
  for (const auto &m : g_pCompositor->m_monitors) {
    if (!m->m_enabled || m->m_isUnsafeFallback)
      continue;
    monitors[m->m_id] = makeUnique<Monitor>(m);
  }
  const auto activeWindow = Desktop::focusState()->window();
  const auto history = Desktop::History::windowTracker()->fullHistory();
  activeMonitor = Desktop::focusState()->monitor()->m_id;
  monitorOffset.snap(activeMonitor);
  for (auto &[monID, mon] : monitors) {
    std::vector<PHLWINDOW> monitorWindows;
    for (auto it = history.rbegin(); it != history.rend(); ++it) {
      auto w = it->lock();
      if (!w)
        continue;
      if (!Config::includeSpecial && w->m_workspace && w->m_workspace->m_isSpecialWorkspace)
        continue;
      if (w->m_isMapped && (!Config::splitMonitor || w->m_monitor.lock() == mon->monitor)) {
        monitorWindows.emplace_back(w);
      }
    }
    const auto it = std::find_if(monitorWindows.begin(), monitorWindows.end(),
                                 [&](const auto &w) { return w == activeWindow || w == mon->monitor->m_activeWorkspace->m_lastFocusedWindow; });
    const int activeIdx = it != monitorWindows.end() ? std::distance(monitorWindows.begin(), it) : 0;
    for (const auto &w : monitorWindows)
      mon->addWindow(w);
    if (!monitorWindows.empty()) {
      mon->activeWindow = activeIdx;
      mon->windows[activeIdx]->isActive = true;
      if (activeIdx > 0)
        mon->rotation.snap((M_PI / 2.0f) + ((2.0f * M_PI * activeIdx) / monitorWindows.size()));
    }
  }
}

bool Manager::isActive() const {
  return active;
}

void Manager::renderDamage(const CRegion &damage) {
  LOG_SCOPE(Log::DAMAGE)

  if (damage.empty())
    return;

  auto dmg = damage;
  auto ext = dmg.getExtents();
  const auto MON = Desktop::focusState()->monitor();

  if (ext.pos() == Vector2D{0, 0} &&
      ext.size().x >= MON->m_transformedSize.x &&
      ext.size().y >= MON->m_transformedSize.y)
    return;

  LOG(Log::DAMAGE, "Damage: {} {}", ext.pos(), ext.size());

  damage.forEachRect([](auto &rect) {
    CBox box = {sc<double>(rect.x1), sc<double>(rect.y1),
                sc<double>(rect.x2 - rect.x1), sc<double>(rect.y2 - rect.y1)};
    g_pHyprOpenGL->renderRect(box, {1.0, 0.0, 0.0, 0.1}, {});
  });
}
