#include "../include/renderpasses.hpp"
#include <algorithm>
#include <hyprland/src/plugins/PluginAPI.hpp>
#include <hyprlang.hpp>
#include <linux/input-event-codes.h>
#include <src/Compositor.hpp>
#include <src/config/ConfigDataValues.hpp>
#include <src/desktop/state/FocusState.hpp>
#include <src/devices/IKeyboard.hpp>
#include <src/managers/input/InputManager.hpp>
#include <xkbcommon/xkbcommon-keysyms.h>

APICALL EXPORT std::string PLUGIN_API_VERSION() {
  return HYPRLAND_API_VERSION;
}

class CarouselManager {
public:
  bool active = false;
  int activeIndex = 0;
  std::vector<UP<WindowContainer>> windows;

  PHLMONITOR lastMonitor = nullptr;

  void toggle() {
    active = !active;
    if (!active)
      deactivate();
  }

  void activate() {
    Log::logger->log(Log::TRACE, "[{}] activate, active: {}, windows.size(): {}", PLUGIN_NAME, active, windows.size());
    if (active || windows.empty())
      return;
    active = true;
    lastMonitor = MONITOR;
    refreshLayout();
    for (auto &el : windows) {
      el->pos = el->targetPos;
      el->size = el->targetSize;
    }
  }

  void deactivate() {
    active = false;
    g_pHyprRenderer->m_renderPass.removeAllOfType("TabCarouselPassElement");
    g_pHyprRenderer->m_renderPass.removeAllOfType("TabCarouselBlurElement");
    if (MONITOR)
      g_pHyprRenderer->damageMonitor(MONITOR);
  }

  void next() {
    if (windows.empty())
      return;
    activeIndex = (activeIndex + 1) % windows.size();
    refreshLayout();
  }

  void prev() {
    if (windows.empty())
      return;
    activeIndex = (activeIndex - 1 + windows.size()) % windows.size();
    refreshLayout();
  }

  void confirm() {
    if (!windows.empty()) {
      auto window = windows[activeIndex]->window;
      Desktop::focusState()->fullWindowFocus(window);
    }
    deactivate();
  }

  void rebuildAll() {
    windows.clear();
    for (auto &el : g_pCompositor->m_windows) {
      if (el->m_isMapped)
        windows.emplace_back(makeUnique<WindowContainer>(el));
    }
  }

  void refreshLayout() {
    Log::logger->log(Log::TRACE, "[{}] refreshLayout entry, active: {}, windows.size(): {}", PLUGIN_NAME, active, windows.size());
    if (!MONITOR || windows.empty())
      return;

    std::vector<WindowContainer *> activeList;
    for (auto &w : windows) {
      if (!w->shouldBeRemoved())
        activeList.push_back(w.get());
    }

    if (activeList.empty())
      return;

    if (activeIndex >= activeList.size())
      activeIndex = std::max((size_t)0, activeList.size() - 1);
    const Vector2D screenCenter = MONITOR->m_position + (MONITOR->m_size / 2.0);
    const double activeHeight = MONITOR->m_size.y * 0.4;
    const double inactiveHeight = MONITOR->m_size.y * 0.3;
    const double spacing = BORDERSIZE + SPACING;
    for (size_t i = 0; i < activeList.size(); ++i) {
      Vector2D winSize = activeList[i]->window->m_realSize->goal();
      auto aspect = winSize.x / std::max(winSize.y, 1.0);
      aspect = std::clamp(aspect, 0.5, 2.0);
      auto targetH = (i == (size_t)activeIndex) ? activeHeight : inactiveHeight;
      activeList[i]->targetSize = Vector2D(targetH * aspect, targetH);
    }

    auto activeWin = activeList[activeIndex];
    activeWin->targetPos = screenCenter - (activeWin->targetSize / 2.0);

    for (int i = activeIndex - 1; i >= 0; --i) {
      activeList[i]->targetPos.x = activeList[i + 1]->targetPos.x - activeList[i]->targetSize.x - spacing;
      activeList[i]->targetPos.y = screenCenter.y - (activeList[i]->targetSize.y / 2.0);
    }

    for (size_t i = activeIndex + 1; i < activeList.size(); ++i) {
      activeList[i]->targetPos.x = activeList[i - 1]->targetPos.x + activeList[i - 1]->targetSize.x + spacing;
      activeList[i]->targetPos.y = screenCenter.y - (activeList[i]->targetSize.y / 2.0);
    }
  }

  void onPreRender() {
    if (!active || windows.empty() || !MONITOR)
      return;

    auto preSize = windows.size();
    std::erase_if(windows, [](auto &el) { return el->shouldBeRemoved(); });
    if (windows.size() != preSize) {
      if (windows.empty()) {
        deactivate();
        return;
      }
      if (activeIndex >= windows.size())
        activeIndex = std::max((size_t)0, windows.size() - 1);
      refreshLayout();
    }

    for (size_t i = 0; i < windows.size(); ++i) {
      auto &el = windows[i];

      el->border->isActive = (i == (size_t)activeIndex);

      Vector2D posDiff = el->targetPos - el->pos;
      Vector2D sizeDiff = el->targetSize - el->size;

      if (posDiff.size() > 0.1 || sizeDiff.size() > 0.1) {
        el->pos = el->pos + posDiff * ANIMATIONSPEED;
        el->size = el->size + sizeDiff * ANIMATIONSPEED;
        el->onResize();
      } else {
        el->pos = el->targetPos;
        el->size = el->targetSize;
        el->onResize();
      }

      el->update();
    }
    // Always damage on pre-render
    g_pHyprRenderer->damageMonitor(MONITOR);
  }

  std::vector<Element *> getRenderList() {
    auto view = std::views::iota(0, (int)windows.size()) | std::views::transform([&](int i) {
                  return std::pair<int, Element *>{std::abs(i - activeIndex), windows[i].get()};
                });

    std::vector<std::pair<int, Element *>> indexed(view.begin(), view.end());

    std::ranges::sort(indexed, std::greater<>{}, [](const auto &p) { return p.first; });

    auto result = indexed | std::views::values;
    return {result.begin(), result.end()};
  }
};

inline static UP<CarouselManager> g_pCarouselManager = makeUnique<CarouselManager>();

static void onPreRender() {
  if (g_pCarouselManager->active) {
    g_pCarouselManager->onPreRender();
  }
}

static void onRender(eRenderStage stage) {
  if (!g_pCarouselManager->active)
    return;

  if (stage == eRenderStage::RENDER_LAST_MOMENT) {
    g_pHyprRenderer->m_renderPass.add(makeUnique<BlurPass>());
    g_pHyprRenderer->m_renderPass.add(makeUnique<RenderPass>(g_pCarouselManager->getRenderList()));
  }
}

static void onWindowCreated(PHLWINDOW w) {
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

static void onMonitorFocus(PHLMONITOR mon) {
  if (mon)
    MONITOR = mon;
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
  g_pCarouselManager->rebuildAll();
}

APICALL EXPORT PLUGIN_DESCRIPTION_INFO PLUGIN_INIT(HANDLE handle) {
  PHANDLE = handle;
  if (const std::string hash = __hyprland_api_get_hash(); hash != __hyprland_api_get_client_hash())
    throw std::runtime_error("Version mismatch");

  static auto PRENDER = HyprlandAPI::registerCallbackDynamic(handle, "render", [&](void *s, SCallbackInfo &i, std::any p) { onRender(std::any_cast<eRenderStage>(p)); });
  static auto PPRERENDER = HyprlandAPI::registerCallbackDynamic(handle, "preRender", [&](void *s, SCallbackInfo &i, std::any p) { onPreRender(); });
  static auto PMONITORADD = HyprlandAPI::registerCallbackDynamic(handle, "monitorAdded", [&](void *s, SCallbackInfo &i, std::any p) { onMonitorAdded(); });
  static auto POPENWINDOW = HyprlandAPI::registerCallbackDynamic(handle, "openWindow", [&](void *s, SCallbackInfo &i, std::any p) { onWindowCreated(std::any_cast<PHLWINDOW>(p)); });
  static auto PCLOSEWINDOW = HyprlandAPI::registerCallbackDynamic(handle, "closeWindow", [&](void *s, SCallbackInfo &i, std::any p) { onWindowClosed(std::any_cast<PHLWINDOW>(p)); });
  static auto PMONITORFOCUS = HyprlandAPI::registerCallbackDynamic(handle, "focusedMon", [&](void *s, SCallbackInfo &i, std::any p) { onMonitorFocus(std::any_cast<PHLMONITOR>(p)); });
  static auto PONRELOAD = HyprlandAPI::registerCallbackDynamic(handle, "configReloaded", [&](void *s, SCallbackInfo &i, std::any p) { onConfigReload(); });

  HyprlandAPI::addConfigValue(PHANDLE, "plugin:alttab:font_size", Hyprlang::INT{24});
  HyprlandAPI::addConfigValue(PHANDLE, "plugin:alttab:border_size", Hyprlang::INT{1});
  HyprlandAPI::addConfigValue(PHANDLE, "plugin:alttab:border_rounding", Hyprlang::INT{0});
  HyprlandAPI::addConfigValue(PHANDLE, "plugin:alttab:border_rounding_power", Hyprlang::FLOAT{2});
  HyprlandAPI::addConfigValue(PHANDLE, "plugin:alttab:border_active", Hyprlang::CConfigCustomValueType{&configHandleGradientSet, &configHandleGradientDestroy, "0xff00ccdd"});
  HyprlandAPI::addConfigValue(PHANDLE, "plugin:alttab:border_inactive", Hyprlang::CConfigCustomValueType{&configHandleGradientSet, &configHandleGradientDestroy, "0xaabbccddff"});
  HyprlandAPI::addConfigValue(PHANDLE, "plugin:alttab:spacing", Hyprlang::INT{10});

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
