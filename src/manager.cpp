#include "../include/manager.hpp"
#include <src/Compositor.hpp>
#include <src/managers/input/InputManager.hpp>

CarouselManager::CarouselManager() {
  Log::logger->log(Log::TRACE, "[{}] CarouselManager ctor", PLUGIN_NAME);
}
void CarouselManager::toggle() {
  active = !active;
  if (!active)
    deactivate();
}

void CarouselManager::activate() {
  if (active)
    return;

  rebuildAll();
  if (monitors.empty())
    return;

  active = true;
  MONITOR = Desktop::focusState()->monitor();
  lastframe = std::chrono::steady_clock::now();

  auto focusedWindow = Desktop::focusState()->window();
  bool foundFocus = false;

  int mIdx = 0;
  for (auto &[id, mon] : monitors) {
    for (size_t wIdx = 0; wIdx < mon.windows.size(); ++wIdx) {
      if (mon.windows[wIdx]->window == focusedWindow) {
        activeMonitorIndex = mIdx;
        mon.activeIndex = wIdx;
        foundFocus = true;
        break;
      }
    }
    if (foundFocus)
      break;
    mIdx++;
  }

  refreshLayout(true);
  g_pCompositor->scheduleFrameForMonitor(MONITOR);
}

void CarouselManager::damageMonitors() {
  for (auto &mon : g_pCompositor->m_monitors) {
    if (!mon || !mon->m_enabled)
      continue;
    g_pHyprRenderer->damageMonitor(mon);
  }
}

void CarouselManager::deactivate() {
  active = false;
  g_pHyprRenderer->m_renderPass.removeAllOfType("TabCarouselPassElement");
  g_pHyprRenderer->m_renderPass.removeAllOfType("TabCarouselBlurElement");
  damageMonitors();
}

void CarouselManager::next(bool snap) {
  if (monitors.empty())
    return;

  auto it = std::next(monitors.begin(), activeMonitorIndex);
  auto &mon = it->second;

  if (mon.windows.empty())
    return;

  mon.activeIndex = (mon.activeIndex + 1) % mon.windows.size();
  refreshLayout(snap);
}

void CarouselManager::prev(bool snap) {
  if (monitors.empty())
    return;

  auto it = std::next(monitors.begin(), activeMonitorIndex);
  auto &mon = it->second;

  if (mon.windows.empty())
    return;

  mon.activeIndex = (mon.activeIndex + mon.windows.size() - 1) % mon.windows.size();
  refreshLayout(snap);
}

void CarouselManager::up(bool snap) {
  if (monitors.empty())
    return;
  activeMonitorIndex = (activeMonitorIndex - 1 + monitors.size()) % monitors.size();
  refreshLayout(snap);
}

void CarouselManager::down(bool snap) {
  if (monitors.empty())
    return;
  activeMonitorIndex = (activeMonitorIndex + 1) % monitors.size();
  refreshLayout(snap);
}

void CarouselManager::confirm() {
  if (!monitors.empty()) {
    auto window = monitors[activeMonitorIndex].windows[monitors[activeMonitorIndex].activeIndex]->window;
    // Fuck the stupid follow mouse behaviour. We force it.
    g_pInputManager->unconstrainMouse();
    window->m_relativeCursorCoordsOnLastWarp = g_pInputManager->getMouseCoordsInternal() - window->m_position;
    Desktop::focusState()->fullWindowFocus(window, Desktop::eFocusReason::FOCUS_REASON_DESKTOP_STATE_CHANGE);
    if (window->m_monitor != MONITOR) {
      window->warpCursor();
      g_pInputManager->m_forcedFocus = window;
      g_pInputManager->simulateMouseMovement();
      g_pInputManager->m_forcedFocus.reset();
      Desktop::focusState()->rawMonitorFocus(MONITOR);
    }
  }
  deactivate();
}

bool CarouselManager::shouldIncludeWindow(PHLWINDOW w) {
  if (INCLUDE_SPECIAL)
    return true;
  return w->m_workspace && !w->m_workspace->m_isSpecialWorkspace;
}
void CarouselManager::rebuildAll() {
  Log::logger->log(Log::TRACE, "[{}] rebuildAll", PLUGIN_NAME);
  monitors.clear();

  for (auto &el : g_pCompositor->m_windows) {
    if (!el || !el->m_isMapped || !el->m_monitor)
      continue;

    if (shouldIncludeWindow(el)) {
      auto id = el->m_monitor->m_id;
      monitors[id].windows.emplace_back(makeUnique<WindowContainer>(el));
    }
  }
  refreshLayout();
}

void CarouselManager::refreshLayout(bool snap) {
  if (!MONITOR || monitors.empty())
    return;

  const auto msize = (MONITOR->m_size * MONITOR->m_scale).round();
  const auto center = msize / 2.0;
  const auto spacing = BORDERSIZE + WINDOW_SPACING;

  const double activeRowH = msize.y * MONITOR_SIZE_ACTIVE;
  const double inactiveRowH = msize.y * MONITOR_SIZE_INACTIVE;

  const double verticalStep = ((activeRowH + (inactiveRowH * WINDOW_SIZE_INACTIVE)) / 2.0) + MONITOR_SPACING;

  int currentRow = 0;
  for (auto &[id, monData] : monitors) {
    auto activeList = monData.windows | std::views::transform([](auto &w) { return w.get(); }) | std::views::filter([](auto *w) { return !w->shouldBeRemoved(); }) | std::ranges::to<std::vector<WindowContainer *>>();

    if (activeList.empty()) {
      currentRow++;
      continue;
    }

    bool isRowSelected = (currentRow == (int)activeMonitorIndex);
    double rowBaseH = isRowSelected ? activeRowH : inactiveRowH;
    double rowCenterY = center.y + (currentRow - (int)activeMonitorIndex) * verticalStep;

    for (size_t j = 0; j < activeList.size(); ++j) {
      auto goal = activeList[j]->window->m_realSize->goal();
      double aspect = std::clamp(goal.x / std::max(goal.y, 1.0), 0.1, 5.0);

      double h = (j == monData.activeIndex && isRowSelected) ? rowBaseH : (rowBaseH * WINDOW_SIZE_INACTIVE);
      activeList[j]->animSize.set(Vector2D(h * aspect, h), snap);
    }

    auto activeWin = activeList[std::min(monData.activeIndex, activeList.size() - 1)];
    activeWin->animPos.set(Vector2D(center.x - (activeWin->animSize.target.x / 2.0), rowCenterY - (activeWin->animSize.target.y / 2.0)), snap);

    auto leftX = activeWin->animPos.target.x;
    for (auto *w : activeList | std::views::take(monData.activeIndex) | std::views::reverse) {
      leftX -= (w->animSize.target.x + spacing);
      w->animPos.set(Vector2D(leftX, rowCenterY - (w->animSize.target.y / 2.0)), snap);
    }

    auto rightX = activeWin->animPos.target.x + activeWin->animSize.target.x;
    for (auto *w : activeList | std::views::drop(monData.activeIndex + 1)) {
      w->animPos.set(Vector2D(rightX + spacing, rowCenterY - (w->animSize.target.y / 2.0)), snap);
      rightX += (w->animSize.target.x + spacing);
    }

    for (auto *w : activeList) {
      bool isFocused = (w == activeWin && isRowSelected);
      w->alpha.set(isFocused ? 1.0f : UNFOCUSEDALPHA, snap);
      w->border->isActive = isFocused;
    }

    currentRow++;
  }
}

bool CarouselManager::isElementOnScreen(WindowContainer *w) {
  if (!MONITOR)
    return false;

  auto mBox = CBox{MONITOR->m_position, MONITOR->m_size}.scale(MONITOR->m_scale);
  auto wBox = CBox{w->pos, w->size};
  return mBox.intersection(wBox).width > 0;
}

void CarouselManager::update() {
  if (!active || monitors.empty())
    return;

  auto now = std::chrono::steady_clock::now();
  double delta = std::chrono::duration_cast<std::chrono::microseconds>(now - lastframe).count() / 1000000.0;
  lastframe = now;

  std::vector<WindowContainer *> needsUpdate;
  size_t currentRowIdx = 0;
  bool anyWindowsLeft = false;

  for (auto it = monitors.begin(); it != monitors.end();) {
    auto &row = it->second;

    std::erase_if(row.windows, [](auto &el) { return el->shouldBeRemoved(); });

    if (row.windows.empty()) {
      it = monitors.erase(it);
      continue;
    }

    if (row.activeIndex >= row.windows.size())
      row.activeIndex = row.windows.empty() ? 0 : row.windows.size() - 1;

    bool isRowActive = (currentRowIdx == activeMonitorIndex);

    for (size_t i = 0; i < row.windows.size(); ++i) {
      auto &w = row.windows[i];
      w->update(delta);

      auto age = now - w->snapshot->lastUpdated;
      bool onScreen = isElementOnScreen(w.get());
      bool isWindowActive = (isRowActive && i == row.activeIndex);

      std::chrono::milliseconds threshold;
      if (isWindowActive) {
        threshold = std::chrono::milliseconds(30);
      } else if (onScreen) {
        threshold = std::chrono::milliseconds(200);
      } else {
        threshold = std::chrono::milliseconds(1000);
      }

      if (!w->snapshot->ready || age > threshold) {
        needsUpdate.push_back(w.get());
      }
    }

    anyWindowsLeft = true;
    currentRowIdx++;
    ++it;
  }

  if (!anyWindowsLeft) {
    deactivate();
    return;
  }

  if (activeMonitorIndex >= monitors.size())
    activeMonitorIndex = monitors.empty() ? 0 : monitors.size() - 1;

  std::sort(needsUpdate.begin(), needsUpdate.end(), [this](auto *a, auto *b) {
    bool aOn = isElementOnScreen(a);
    bool bOn = isElementOnScreen(b);
    if (aOn != bOn)
      return aOn;
    return a->snapshot->lastUpdated < b->snapshot->lastUpdated;
  });

  int processed = 0;
  const int FRAME_BUDGET = 2;
  for (auto *w : needsUpdate) {
    if (processed >= FRAME_BUDGET)
      break;
    w->snapshot->snapshot();
    processed++;
  }
}

std::vector<Element *> CarouselManager::getRenderList() {
  std::vector<std::pair<int, Element *>> indexed;

  size_t rowIndex = 0;
  for (auto &[id, monData] : monitors) {
    int rowDist = std::abs(static_cast<int>(rowIndex) - static_cast<int>(activeMonitorIndex));

    for (size_t winIndex = 0; winIndex < monData.windows.size(); ++winIndex) {
      int winDist = std::abs(static_cast<int>(winIndex) - static_cast<int>(monData.activeIndex));

      int priority = (rowDist * 1000) + winDist;
      indexed.push_back({priority, monData.windows[winIndex].get()});
    }
    rowIndex++;
  }

  std::ranges::sort(indexed, std::greater<>{}, [](const auto &p) { return p.first; });

  std::vector<Element *> result;
  for (auto &p : indexed) {
    result.push_back(p.second);
  }

  return result;
}

bool CarouselManager::windowPicked(Vector2D mousePos) {
  auto list = getRenderList();

  for (auto it = list.rbegin(); it != list.rend(); ++it) {
    WindowContainer *wc = dynamic_cast<WindowContainer *>(*it);
    if (!wc)
      continue;

    CBox windowBox = {wc->pos, wc->size};

    if (windowBox.containsPoint(mousePos)) {
      updateSelection(wc);
      return true;
    }
  }
  return false;
}

void CarouselManager::updateSelection(WindowContainer *target) {
  size_t rIdx = 0;
  for (auto &[id, mon] : monitors) {
    for (size_t wIdx = 0; wIdx < mon.windows.size(); ++wIdx) {
      if (mon.windows[wIdx].get() == target) {
        activeMonitorIndex = rIdx;
        mon.activeIndex = wIdx;
        refreshLayout();
        return;
      }
    }
    rIdx++;
  }
}
