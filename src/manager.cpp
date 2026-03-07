#include "manager.hpp"
#include "defines.hpp"
#include "helpers.hpp"
#include <aquamarine/output/Output.hpp>
#include <chrono>
#include <hyprutils/math/Vector2D.hpp>
#include <src/Compositor.hpp>
#include <src/desktop/history/WindowHistoryTracker.hpp>
#include <src/desktop/state/FocusState.hpp>
#include <src/helpers/Color.hpp>
#include <src/helpers/Monitor.hpp>
#include <src/managers/eventLoop/EventLoopManager.hpp>
#include <src/managers/input/InputManager.hpp>
#include <src/plugins/PluginAPI.hpp>
#include <src/protocols/PresentationTime.hpp>
#include <src/render/pass/RectPassElement.hpp>
#include <src/render/pass/TexPassElement.hpp>
#define private public
#include <src/render/OpenGL.hpp>
#include <src/render/Renderer.hpp>
#undef private

#ifndef NDEBUG
static int counter = 0;
static int lastCounter = 0;
#endif

Manager::Manager() {
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
  // g_pHyprRenderer->damageMonitor(Desktop::focusState()->monitor());
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
  loopTimer = makeShared<CEventLoopTimer>(std::chrono::milliseconds(10), [this](SP<CEventLoopTimer> timer, void *data) {
    auto d = FloatTime(NOW - lastFrame).count();
    auto min = std::min(d, (monitors[activeMonitor]->monitor->m_refreshRate * 2) / 1000);
    update(min);
    lastFrame = NOW;
    loopTimer->updateTimeout(std::chrono::milliseconds(16)); }, nullptr);
  g_pEventLoopManager->addTimer(loopTimer);
}

void Manager::deactivate() {
  LOG_SCOPE()
  active = false;
  graceTimer->cancel();
  for (const auto &[id, mon] : monitors) {
    g_pHyprRenderer->damageMonitor(mon->monitor);
  }
  loopTimer.reset();
  graceTimer.reset();
  stack.clear();
  monitors.clear();
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
#else
    Desktop::focusState()->fullWindowFocus(selected.lock(), Desktop::FOCUS_REASON_KEYBIND);
#endif
  }

  deactivate();
}

void Manager::update(float delta) {
  LOG_SCOPE(Log::UPDATE)
  const auto MONITOR = Desktop::focusState()->monitor();
  const Vector2D monitorPos = MONITOR->m_position;

  monitorFade.tick(delta, 0.4);
  monitorOffset.tick(delta, Config::monitorAnimationSpeed);

  const float spacing = MONITOR->m_size.y * Config::monitorSpacing;

  stack.clear();
  int i = 0;
  for (auto &[id, mon] : monitors) {
    float off = (i - monitorOffset.current) * spacing;
    mon->position = {monitorPos.x, monitorPos.y + off, MONITOR->m_size.x, MONITOR->m_size.y};
    float z = (id == activeMonitor) ? 1000.0f : -std::abs(i - monitorOffset.current);
    mon->update(delta, {monitorPos.x, monitorPos.y + off});

    if (mon->animating || !monitorOffset.done()) {
      g_pHyprRenderer->damageMonitor(MONITOR);
    }
    i++;
    stack.push_back({.monitor = mon.get(), .offset = off, .z = z});
  }

  std::sort(stack.begin(), stack.end(), [](const auto &a, const auto &b) {
    return a.z < b.z;
  });
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

  damageMonitors();
}

void Manager::draw(MONITORID monid, const CRegion &damage) {
  const auto cur = Desktop::focusState()->monitor();
  if (monitors.empty() || !monitors.contains(monid) || !monitors[monid]->monitor)
    return;
  renderBackground(monid, damage);
  if (!Config::splitMonitor)
    monitors[monid]->draw(damage, monitorFade.current);
  else if (monid == cur->m_id)
    renderMonitors(damage);

#ifndef NDEBUG
  Overlay->add(std::format("ActiveID: {}, Offset: {:.2f}", activeMonitor, monitorOffset.current));
  Overlay->draw(cur);
#endif
}

void Manager::renderBackground(MONITORID monid, const CRegion &damage) {
  if (Config::powersave) {
    monitors[monid]->renderTexture(damage);
    return;
  }
  auto fakeDamage = damage;
  g_pHyprOpenGL->renderRect(fakeDamage.getExtents(),
                            CHyprColor(0.0, 0.0, 0.0, Config::dimEnabled ? Config::dimAmount : 0),
                            {.blur = sc<bool>(Config::blurBG)});
}

void Manager::renderMonitors(const CRegion &damage) {
  for (auto &el : stack) {
    el.monitor->draw(damage, monitorFade.current);
  }
}

void Manager::onConfigReload() {
#define X(type, name, conf, def) \
  Config::name = *CConfigValue<Hyprlang::type>("plugin:alttab:" conf);
  CONFIG_VARS
#undef X

  Config::activeBorderColor = rc<CGradientValueData *>(std::any_cast<void *>(HyprlandAPI::getConfigValue(PHANDLE, "plugin:alttab:border_active")->getValue()));
  Config::inactiveBorderColor = rc<CGradientValueData *>(std::any_cast<void *>(HyprlandAPI::getConfigValue(PHANDLE, "plugin:alttab:border_inactive")->getValue()));
  stack.clear();
}

void Manager::onWindowCreated(PHLWINDOW window) {
  // TODO: add window to specific monitor
  rebuild();
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
  case eRenderStage::RENDER_PRE: {
    ;
  } break;
  case eRenderStage::RENDER_LAST_MOMENT:
    g_pHyprRenderer->m_renderPass.add(makeUnique<RenderPass>());
    break;
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

  const Vector2D localMouse = (mousePos - MONITOR->m_position) * MONITOR->m_scale;

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
  setLayout();
  for (const auto &m : g_pCompositor->m_monitors) {
    if (!m->m_enabled || m->m_isUnsafeFallback)
      continue;
    monitors[m->m_id] = makeUnique<Monitor>(m);
  }

  // auto activeWindow = Desktop::focusState()->window();
  PHLWINDOWREF activeWindow;

  const auto history = Desktop::History::windowTracker()->fullHistory();
  /*
  if (history.size() >= 2) {
    activeWindow = *(history | std::views::reverse | std::views::drop(1)).begin();
  } else {
    activeWindow = Desktop::focusState()->window();
  }
  */
  activeWindow = Desktop::focusState()->window();
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
    /*
    // TODO: Find a better fallback in-case some window is in the history but not mapped..
    for (const auto &w : g_pCompositor->m_windows) {
      if (std::find(monitorWindows.begin(), monitorWindows.end(), w) == monitorWindows.end()) {
        if (!Config::includeSpecial && w->m_workspace && w->m_workspace->m_isSpecialWorkspace)
          continue;

        if (w->m_isMapped && (!Config::splitMonitor || w->m_monitor.lock() == mon->monitor)) {
          monitorWindows.emplace_back(w);
        }
      }
    }
    */
    // i reall should clean this up and make a setActive..
    for (const auto &w : monitorWindows) {
      auto card = mon->addWindow(w);
      if (w == activeWindow) {
        mon->activeWindow = mon->windows.size() - 1;
        const int count = monitorWindows.size();
        const float angle = (M_PI / 2.0f) + ((2.0f * M_PI * mon->activeWindow) / count);
        mon->rotation.snap(angle);
        mon->windows.back()->isActive = true;
      } else if (w == mon->monitor->m_activeWorkspace->m_lastFocusedWindow) {
        mon->activeWindow = mon->windows.size() - 1;
        mon->windows.back()->isActive = true;
      }
    }
    g_pHyprRenderer->damageMonitor(mon->monitor);
    // damageMonitor should do this??
    // g_pCompositor->scheduleFrameForMonitor(mon->monitor);
  }
}

bool Manager::isActive() const {
  return active;
}

void RenderPass::draw(const CRegion &damage) {
  const auto MON = g_pHyprOpenGL->m_renderData.pMonitor;
  manager->draw(MON->m_id, damage);
}
