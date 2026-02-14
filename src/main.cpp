#include "../include/renderpasses.hpp"
#include <algorithm>
#include <hyprland/src/plugins/PluginAPI.hpp>
#include <hyprlang.hpp>
#include <linux/input-event-codes.h>
#include <src/Compositor.hpp>
#include <src/config/ConfigDataValues.hpp>
#include <src/desktop/state/FocusState.hpp>
#include <src/devices/IKeyboard.hpp>
#include <src/managers/PointerManager.hpp>
#include <src/managers/input/InputManager.hpp>
#include <src/render/Renderer.hpp>
#include <xkbcommon/xkbcommon-keysyms.h>

APICALL EXPORT std::string PLUGIN_API_VERSION() {
  return HYPRLAND_API_VERSION;
}

class CarouselManager {
public:
  bool active = false;
  size_t activeIndex = 0;
  std::chrono::time_point<std::chrono::steady_clock> lastframe = std::chrono::steady_clock::now();
  std::vector<UP<WindowContainer>> windows;

  void toggle() {
    active = !active;
    if (!active)
      deactivate();
  }

  void activate() {
    Log::logger->log(Log::TRACE, "[{}] activate, active: {}, windows.size(): {}", PLUGIN_NAME, active, windows.size());
    if (active)
      return;
    rebuildAll(); // fresh list so windows moved to/from special are correct without config reload
    if (windows.empty())
      return;
    active = true;
    MONITOR = Desktop::focusState()->monitor();
    lastframe = std::chrono::steady_clock::now();
    refreshLayout(true);
  }

  void damageMonitors() {
    for (auto &mon : g_pCompositor->m_monitors) {
      if (!mon || !mon->m_enabled)
        continue;
      g_pHyprRenderer->damageMonitor(mon);
    }
  }

  void deactivate() {
    active = false;
    g_pHyprRenderer->m_renderPass.removeAllOfType("TabCarouselPassElement");
    g_pHyprRenderer->m_renderPass.removeAllOfType("TabCarouselBlurElement");
    damageMonitors();
  }

  void next(bool snap = false) {
    if (windows.empty())
      return;
    activeIndex = (activeIndex + 1) % windows.size();
    refreshLayout(snap);
  }

  void prev(bool snap = false) {
    if (windows.empty())
      return;
    activeIndex = (activeIndex - 1 + windows.size()) % windows.size();
    refreshLayout(snap);
  }

  void confirm() {
    if (!windows.empty()) {
      auto window = windows[activeIndex]->window;
      Desktop::focusState()->fullWindowFocus(window);
    }
    deactivate();
  }

  static bool shouldIncludeWindow(PHLWINDOW w) {
    if (INCLUDE_SPECIAL)
      return true;
    return w->m_workspace && !w->m_workspace->m_isSpecialWorkspace;
  }

  void rebuildAll() {
    windows.clear();
    for (auto &el : g_pCompositor->m_windows) {
      if (el->m_isMapped && shouldIncludeWindow(el))
        windows.emplace_back(makeUnique<WindowContainer>(el));
    }
  }

  void refreshLayout(bool snap = false) {
    if (!MONITOR || windows.empty())
      return;

    auto activeList = windows | std::views::transform([](auto &w) { return w.get(); }) | std::views::filter([](auto *w) { return !w->shouldBeRemoved(); }) | std::ranges::to<std::vector<WindowContainer *>>();

    if (activeList.empty())
      return;

    activeIndex = std::clamp(activeIndex, (size_t)0, activeList.size() - 1);

    const auto msize = (MONITOR->m_size * MONITOR->m_scale).round();
    const auto center = (msize / 2.0);
    const auto spacing = BORDERSIZE + SPACING;

    for (size_t i = 0; i < activeList.size(); ++i) {
      auto winSize = activeList[i]->window->m_realSize->goal();
      auto aspect = std::clamp(winSize.x / std::max(winSize.y, 1.0), 0.5, 2.0);
      auto targetH = (i == activeIndex) ? msize.y * 0.4 : msize.y * 0.3;
      activeList[i]->animSize.set(Vector2D(targetH * aspect, targetH), snap);
    }

    auto activeWin = activeList[activeIndex];
    auto activePos = center - (activeWin->animSize.target / 2.0);
    activeWin->animPos.set(activePos, snap);

    auto leftX = activeWin->animPos.target.x;
    for (auto *w : activeList | std::views::take(activeIndex) | std::views::reverse) {
      Vector2D p = {leftX - w->animSize.target.x - spacing, center.y - (w->animSize.target.y / 2.0)};
      w->animPos.set(p, snap);
      leftX = p.x;
    }

    auto rightX = activeWin->animPos.target.x + activeWin->animSize.target.x;
    for (auto *w : activeList | std::views::drop(activeIndex + 1)) {
      Vector2D p = {rightX + spacing, center.y - (w->animSize.target.y / 2.0)};
      w->animPos.set(p, snap);
      rightX = p.x + w->animSize.target.x;
    }

    for (auto *w : activeList) {
      auto isActive = w == activeWin;
      w->alpha.set(isActive ? 1.0 : UNFOCUSEDALPHA, snap);
      w->border->isActive = isActive;
    }
  }

  bool isElementOnScreen(WindowContainer *w) {
    if (!MONITOR)
      return false;

    auto mBox = CBox{MONITOR->m_position, MONITOR->m_size}.scale(MONITOR->m_scale);
    auto wBox = CBox{w->pos, w->size};
    return mBox.intersection(wBox).width > 0;
  }

  void update() {
    if (!active || windows.empty())
      return;

    auto now = std::chrono::steady_clock::now();
    double delta = std::chrono::duration_cast<std::chrono::microseconds>(now - lastframe).count() / 1000000.0;
    lastframe = now;

    auto preSize = windows.size();
    std::erase_if(windows, [](auto &el) { return el->shouldBeRemoved(); });
    if (windows.size() != preSize) {
      if (windows.empty()) {
        deactivate();
        return;
      }
      if (activeIndex >= windows.size())
        prev(true);
    }

    std::vector<WindowContainer *> needsUpdate;

    for (auto &w : windows) {
      w->update(delta);

      auto age = now - w->snapshot->lastUpdated;
      bool onScreen = isElementOnScreen(w.get());
      bool isActive = (w == windows[activeIndex]);

      std::chrono::milliseconds threshold;
      if (isActive) {
        // 30fps
        threshold = std::chrono::milliseconds(30);
      } else if (onScreen) {
        // 5fps
        threshold = std::chrono::milliseconds(200);
      } else {
        // 1fps
        threshold = std::chrono::milliseconds(1000);
      }

      if (!w->snapshot->ready || age > threshold) {
        needsUpdate.push_back(w.get());
      }
    }

    std::sort(needsUpdate.begin(), needsUpdate.end(), [this](auto *a, auto *b) {
      bool aOn = isElementOnScreen(a);
      bool bOn = isElementOnScreen(b);

      if (aOn != bOn)
        return aOn;

      if (a == windows[activeIndex].get())
        return true;
      if (b == windows[activeIndex].get())
        return false;

      return a->snapshot->lastUpdated < b->snapshot->lastUpdated;
    });

    int processed = 0;
    const int FRAME_BUDGET = 3;

    for (auto *w : needsUpdate) {
      if (processed >= FRAME_BUDGET)
        break;
      w->snapshot->snapshot();
      processed++;
    }
  }

  std::vector<Element *> getRenderList() {
    auto view = std::views::iota(0, (int)windows.size()) | std::views::transform([&](int i) {
                  return std::pair<int, Element *>{std::abs(i - (int)activeIndex), windows[i].get()};
                });
    std::vector<std::pair<int, Element *>> indexed(view.begin(), view.end());
    std::ranges::sort(indexed, std::greater<>{}, [](const auto &p) { return p.first; });
    auto result = indexed | std::views::values;
    return {result.begin(), result.end()};
  }
};

inline static UP<CarouselManager> g_pCarouselManager = makeUnique<CarouselManager>();

static void onRender(eRenderStage stage) {
  if (!g_pCarouselManager->active)
    return;

  if (stage == eRenderStage::RENDER_PRE) {
    g_pHyprRenderer->setCursorHidden(true);
    g_pCarouselManager->update();
  }

  if (stage == eRenderStage::RENDER_LAST_MOMENT) {
    g_pHyprRenderer->m_renderPass.add(makeUnique<BlurPass>());
    g_pHyprRenderer->m_renderPass.add(makeUnique<RenderPass>(g_pCarouselManager->getRenderList()));
    g_pHyprRenderer->setCursorHidden(false);
  }

  g_pCarouselManager->damageMonitors();
}

static void onWindowCreated(PHLWINDOW w) {
  if (!CarouselManager::shouldIncludeWindow(w))
    return;
  g_pCarouselManager->windows.emplace_back(makeUnique<WindowContainer>(w));
  g_pCarouselManager->refreshLayout();
}

static void onWindowClosed(PHLWINDOW w) {
  for (auto &el : g_pCarouselManager->windows) {
    if (el->window == w) {
      el->markForRemoval(); // Safe and clean
      break;
    }
  }
  g_pCarouselManager->refreshLayout();
}

static void onMonitorAdded() {
  ;
  // TODO: Add monitor row
}

static CFunctionHook *keyhookfn = nullptr;
typedef bool (*CKeybindManager_onKeyEvent)(void *self, std::any &event, SP<IKeyboard> pKeyboard);

static bool onKeyEvent(void *self, std::any event, SP<IKeyboard> pKeyboard) {
  if (!keyhookfn || !keyhookfn->m_original)
    return true;

  auto e = std::any_cast<IKeyboard::SKeyEvent>(event);
  const auto MODS = g_pInputManager->getModsFromAllKBs();

  if (!g_pCarouselManager->active && e.state == WL_KEYBOARD_KEY_STATE_PRESSED) {
    if (e.keycode == 15 && (MODS & HL_MODIFIER_ALT)) {
      g_pCarouselManager->activate();
      g_pCarouselManager->next();
      return false;
    }
  }

  if (!g_pCarouselManager->active)
    return ((CKeybindManager_onKeyEvent)keyhookfn->m_original)(self, event, pKeyboard);

  const auto KEYSYM = xkb_state_key_get_one_sym(pKeyboard->m_xkbState, e.keycode + 8);

  if (e.state == WL_KEYBOARD_KEY_STATE_PRESSED) {
    switch (KEYSYM) {
    case XKB_KEY_Tab:
    case XKB_KEY_ISO_Left_Tab:
    case XKB_KEY_d:
    case XKB_KEY_Right:
    case XKB_KEY_s:
    case XKB_KEY_Down:
      (MODS & HL_MODIFIER_SHIFT) ? g_pCarouselManager->prev() : g_pCarouselManager->next();
      break;

    case XKB_KEY_a:
    case XKB_KEY_Left:
    case XKB_KEY_w:
    case XKB_KEY_Up:
      g_pCarouselManager->prev();
      break;

    case XKB_KEY_Return:
    case XKB_KEY_space:
      g_pCarouselManager->confirm();
      break;

    case XKB_KEY_Escape:
      g_pCarouselManager->deactivate();
      break;
    }
  } else {
    if (KEYSYM == XKB_KEY_Alt_L || KEYSYM == XKB_KEY_Alt_R || KEYSYM == XKB_KEY_Super_L) {
      g_pCarouselManager->confirm();
    }
  }

  return false;
}

// Straight from ConfigManager.cpp. THANKS GUYS!
static Hyprlang::CParseResult configHandleGradientSet(const char *VALUE, void **data) {
  std::string V = VALUE;

  if (!*data)
    *data = new CGradientValueData();

  const auto DATA = sc<CGradientValueData *>(*data);

  CVarList2 varlist(std::string(V), 0, ' ');
  DATA->m_colors.clear();

  std::string parseError = "";

  for (auto const &var : varlist) {
    if (var.find("deg") != std::string::npos) {
      try {
        DATA->m_angle = std::stoi(std::string(var.substr(0, var.find("deg")))) * (PI / 180.0); // radians
      } catch (...) {
        Log::logger->log(Log::WARN, "Error parsing gradient {}", V);
        parseError = "Error parsing gradient " + V;
      }

      break;
    }

    if (DATA->m_colors.size() >= 10) {
      Log::logger->log(Log::WARN, "Error parsing gradient {}: max colors is 10.", V);
      parseError = "Error parsing gradient " + V + ": max colors is 10.";
      break;
    }

    try {
      const auto COL = configStringToInt(std::string(var));
      if (!COL)
        throw std::runtime_error(std::format("failed to parse {} as a color", var));
      DATA->m_colors.emplace_back(COL.value());
    } catch (std::exception &e) {
      Log::logger->log(Log::WARN, "Error parsing gradient {}", V);
      parseError = "Error parsing gradient " + V + ": " + e.what();
    }
  }

  if (DATA->m_colors.empty()) {
    Log::logger->log(Log::WARN, "Error parsing gradient {}", V);
    if (parseError.empty())
      parseError = "Error parsing gradient " + V + ": No colors?";

    DATA->m_colors.emplace_back(0); // transparent
  }

  DATA->updateColorsOk();

  Hyprlang::CParseResult result;
  if (!parseError.empty())
    result.setError(parseError.c_str());

  return result;
}

static void configHandleGradientDestroy(void **data) {
  if (*data)
    delete sc<CGradientValueData *>(*data);
}

static void onConfigReload() {
  Log::logger->log(Log::TRACE, "[{}] onConfigReload", PLUGIN_NAME);
  FONTSIZE = std::any_cast<Hyprlang::INT>(HyprlandAPI::getConfigValue(PHANDLE, "plugin:alttab:font_size")->getValue());
  BORDERSIZE = std::any_cast<Hyprlang::INT>(HyprlandAPI::getConfigValue(PHANDLE, "plugin:alttab:border_size")->getValue());
  BORDERROUNDING = std::any_cast<Hyprlang::INT>(HyprlandAPI::getConfigValue(PHANDLE, "plugin:alttab:border_rounding")->getValue());
  BORDERROUNDINGPOWER = std::any_cast<Hyprlang::FLOAT>(HyprlandAPI::getConfigValue(PHANDLE, "plugin:alttab:border_rounding_power")->getValue());
  ACTIVEBORDERCOLOR = rc<CGradientValueData *>(std::any_cast<void *>(HyprlandAPI::getConfigValue(PHANDLE, "plugin:alttab:border_active")->getValue()));
  INACTIVEBORDERCOLOR = rc<CGradientValueData *>(std::any_cast<void *>(HyprlandAPI::getConfigValue(PHANDLE, "plugin:alttab:border_inactive")->getValue()));
  SPACING = std::any_cast<Hyprlang::INT>(HyprlandAPI::getConfigValue(PHANDLE, "plugin:alttab:spacing")->getValue());
  ANIMATIONSPEED = std::any_cast<Hyprlang::FLOAT>(HyprlandAPI::getConfigValue(PHANDLE, "plugin:alttab:animation_speed")->getValue());
  UNFOCUSEDALPHA = std::any_cast<Hyprlang::FLOAT>(HyprlandAPI::getConfigValue(PHANDLE, "plugin:alttab:unfocused_alpha")->getValue());
  INCLUDE_SPECIAL = std::any_cast<Hyprlang::INT>(HyprlandAPI::getConfigValue(PHANDLE, "plugin:alttab:include_special")->getValue()) != 0;
  g_pCarouselManager->rebuildAll();
}

APICALL EXPORT PLUGIN_DESCRIPTION_INFO PLUGIN_INIT(HANDLE handle) {
  PHANDLE = handle;
  if (const std::string hash = __hyprland_api_get_hash(); hash != __hyprland_api_get_client_hash())
    throw std::runtime_error("Version mismatch");

  static auto PRENDER = HyprlandAPI::registerCallbackDynamic(handle, "render", [&](void *s, SCallbackInfo &i, std::any p) { onRender(std::any_cast<eRenderStage>(p)); });
  static auto PMONITORADD = HyprlandAPI::registerCallbackDynamic(handle, "monitorAdded", [&](void *s, SCallbackInfo &i, std::any p) { onMonitorAdded(); });
  static auto POPENWINDOW = HyprlandAPI::registerCallbackDynamic(handle, "openWindow", [&](void *s, SCallbackInfo &i, std::any p) { onWindowCreated(std::any_cast<PHLWINDOW>(p)); });
  static auto PCLOSEWINDOW = HyprlandAPI::registerCallbackDynamic(handle, "closeWindow", [&](void *s, SCallbackInfo &i, std::any p) { onWindowClosed(std::any_cast<PHLWINDOW>(p)); });
  static auto PONRELOAD = HyprlandAPI::registerCallbackDynamic(handle, "configReloaded", [&](void *s, SCallbackInfo &i, std::any p) { onConfigReload(); });

  HyprlandAPI::addConfigValue(PHANDLE, "plugin:alttab:font_size", Hyprlang::INT{24});
  HyprlandAPI::addConfigValue(PHANDLE, "plugin:alttab:border_size", Hyprlang::INT{1});
  HyprlandAPI::addConfigValue(PHANDLE, "plugin:alttab:border_rounding", Hyprlang::INT{0});
  HyprlandAPI::addConfigValue(PHANDLE, "plugin:alttab:border_rounding_power", Hyprlang::FLOAT{2});
  HyprlandAPI::addConfigValue(PHANDLE, "plugin:alttab:border_active", Hyprlang::CConfigCustomValueType{&configHandleGradientSet, &configHandleGradientDestroy, "0xff00ccdd"});
  HyprlandAPI::addConfigValue(PHANDLE, "plugin:alttab:border_inactive", Hyprlang::CConfigCustomValueType{&configHandleGradientSet, &configHandleGradientDestroy, "0xaabbccddff"});
  HyprlandAPI::addConfigValue(PHANDLE, "plugin:alttab:spacing", Hyprlang::INT{10});
  HyprlandAPI::addConfigValue(PHANDLE, "plugin:alttab:animation_speed", Hyprlang::FLOAT{1.0});
  HyprlandAPI::addConfigValue(PHANDLE, "plugin:alttab:unfocused_alpha", Hyprlang::FLOAT{0.6});
  HyprlandAPI::addConfigValue(PHANDLE, "plugin:alttab:include_special", Hyprlang::INT{1});

  HyprlandAPI::reloadConfig();
  onConfigReload();

  MONITOR = Desktop::focusState()->monitor();

  auto keyhooklookup = HyprlandAPI::findFunctionsByName(PHANDLE, "onKeyEvent");
  if (keyhooklookup.size() != 1) {
    for (auto &f : keyhooklookup)
      Log::logger->log(Log::ERR, "onKeyEvent found at {} :: sig: {}, demangled: {}", f.address, f.signature, f.demangled);
    throw std::runtime_error("CKeybindManager::onKeyEvent not found");
  }
  Log::logger->log(Log::ERR, "onKeyEvent found at {} :: sig: {}, demangled: {}", keyhooklookup[0].address, keyhooklookup[0].signature,
                   keyhooklookup[0].demangled);
  keyhookfn = HyprlandAPI::createFunctionHook(PHANDLE, keyhooklookup[0].address, (void *)onKeyEvent);
  auto success = keyhookfn->hook();
  if (!success)
    throw std::runtime_error("Failed to hook CKeybindManager::onKeyEvent");

  return {PLUGIN_NAME, PLUGIN_DESCRIPTION, PLUGIN_AUTHOR, PLUGIN_VERSION};
}

APICALL EXPORT void PLUGIN_EXIT() {
  g_pCarouselManager.reset();
}
