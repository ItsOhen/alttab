#include "../include/manager.hpp"
#include "../include/renderpasses.hpp"
#include <hyprland/src/managers/KeybindManager.hpp>
#include <hyprland/src/plugins/PluginAPI.hpp>
#include <hyprlang.hpp>
#include <linux/input-event-codes.h>
#include <src/Compositor.hpp>
#include <src/config/ConfigDataValues.hpp>
#include <src/desktop/state/FocusState.hpp>
#include <src/devices/IKeyboard.hpp>
#include <src/managers/input/InputManager.hpp>
#include <src/render/Renderer.hpp>
#include <xkbcommon/xkbcommon-keysyms.h>

APICALL EXPORT std::string PLUGIN_API_VERSION() {
  return HYPRLAND_API_VERSION;
}

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
  if (!CarouselManager::shouldIncludeWindow(w) || !w->m_monitor)
    return;

  int id = w->m_monitor->m_id;
  g_pCarouselManager->monitors[id].windows.emplace_back(makeUnique<WindowContainer>(w));
  g_pCarouselManager->refreshLayout();
}

static void onWindowClosed(PHLWINDOW w) {
  bool found = false;
  for (auto &[id, mon] : g_pCarouselManager->monitors) {
    for (auto &el : mon.windows) {
      if (el->window == w) {
        el->markForRemoval();
        found = true;
        break;
      }
    }
    if (found)
      break;
  }
  g_pCarouselManager->refreshLayout();
}

static void onWindowMoved(std::any p) {
  if (!g_pCarouselManager->active)
    return;

  try {
    auto args = std::any_cast<std::vector<std::any>>(p);
    if (args.empty())
      return;

    auto w = std::any_cast<PHLWINDOW>(args[0]);
    if (!w)
      return;

    Log::logger->log(Log::TRACE, "[{}] onWindowMoved for window: {}", PLUGIN_NAME, w->m_title);

    g_pCarouselManager->rebuildAll();
  } catch (const std::bad_any_cast &e) {
    Log::logger->log(Log::ERR, "[{}] onWindowMoved: Cast failed: {}", PLUGIN_NAME, e.what());
  }
}

static void onMonitorAdded() {
  ;
  // TODO: Add monitor row
}

// STUPID FOCUSTATE BUG!
static void onMonitorFocusChange(PHLMONITOR m) {
  MONITOR = m;
}

CFunctionHook *keyhookfn = nullptr;
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
      (MODS & HL_MODIFIER_SHIFT) ? g_pCarouselManager->prev() : g_pCarouselManager->next();
      break;

    case XKB_KEY_Down:
    case XKB_KEY_s:
      g_pCarouselManager->down();
      break;

    case XKB_KEY_a:
    case XKB_KEY_Left:
      g_pCarouselManager->prev();
      break;

    case XKB_KEY_w:
    case XKB_KEY_Up:
      g_pCarouselManager->up();
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

static void onMouseClick(SCallbackInfo &i, std::any p) {
  if (!g_pCarouselManager->active)
    return;

  auto e = std::any_cast<IPointer::SButtonEvent>(p);
  if (e.state != WL_POINTER_BUTTON_STATE_PRESSED)
    return;

  i.cancelled = true;
  Vector2D mouseCoords = g_pInputManager->getMouseCoordsInternal();

  auto list = g_pCarouselManager->getRenderList();
  for (auto *el : list) {
    if (el->shouldBeRemoved())
      continue;

    if (el->onMouseClick(mouseCoords)) {
      break;
    }
  }
}

static void onMouseMove(SCallbackInfo &i, std::any p) {
  if (!g_pCarouselManager->active)
    return;
  Vector2D mousePos = g_pInputManager->getMouseCoordsInternal();

  auto list = g_pCarouselManager->getRenderList();
  for (auto *el : list) {
    el->onMouseMove(mousePos);
  }
};

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
  WINDOW_SPACING = std::any_cast<Hyprlang::INT>(HyprlandAPI::getConfigValue(PHANDLE, "plugin:alttab:window_spacing")->getValue());
  WINDOW_SIZE_INACTIVE = std::any_cast<Hyprlang::FLOAT>(HyprlandAPI::getConfigValue(PHANDLE, "plugin:alttab:window_size_inactive")->getValue());
  MONITOR_SPACING = std::any_cast<Hyprlang::INT>(HyprlandAPI::getConfigValue(PHANDLE, "plugin:alttab:monitor_spacing")->getValue());
  MONITOR_SIZE_ACTIVE = std::any_cast<Hyprlang::FLOAT>(HyprlandAPI::getConfigValue(PHANDLE, "plugin:alttab:monitor_size_active")->getValue());
  MONITOR_SIZE_INACTIVE = std::any_cast<Hyprlang::FLOAT>(HyprlandAPI::getConfigValue(PHANDLE, "plugin:alttab:monitor_size_inactive")->getValue());
  ANIMATIONSPEED = std::any_cast<Hyprlang::FLOAT>(HyprlandAPI::getConfigValue(PHANDLE, "plugin:alttab:animation_speed")->getValue());
  UNFOCUSEDALPHA = std::any_cast<Hyprlang::FLOAT>(HyprlandAPI::getConfigValue(PHANDLE, "plugin:alttab:unfocused_alpha")->getValue());
  INCLUDE_SPECIAL = std::any_cast<Hyprlang::INT>(HyprlandAPI::getConfigValue(PHANDLE, "plugin:alttab:include_special")->getValue()) != 0;
  g_pCarouselManager->rebuildAll();
}

APICALL EXPORT PLUGIN_DESCRIPTION_INFO PLUGIN_INIT(HANDLE handle) {
  PHANDLE = handle;
  if (const std::string hash = __hyprland_api_get_hash(); hash != __hyprland_api_get_client_hash())
    throw std::runtime_error("Version mismatch");
  try {
    static auto PRENDER = HyprlandAPI::registerCallbackDynamic(handle, "render", [&](void *s, SCallbackInfo &i, std::any p) { onRender(std::any_cast<eRenderStage>(p)); });
    static auto PMONITORADD = HyprlandAPI::registerCallbackDynamic(handle, "monitorAdded", [&](void *s, SCallbackInfo &i, std::any p) { onMonitorAdded(); });
    static auto POPENWINDOW = HyprlandAPI::registerCallbackDynamic(handle, "openWindow", [&](void *s, SCallbackInfo &i, std::any p) { onWindowCreated(std::any_cast<PHLWINDOW>(p)); });
    static auto PCLOSEWINDOW = HyprlandAPI::registerCallbackDynamic(handle, "closeWindow", [&](void *s, SCallbackInfo &i, std::any p) { onWindowClosed(std::any_cast<PHLWINDOW>(p)); });
    static auto PONWINDOWMOVED = HyprlandAPI::registerCallbackDynamic(handle, "moveWindow", [&](void *s, SCallbackInfo &i, std::any p) { onWindowMoved(p); });
    static auto PONRELOAD = HyprlandAPI::registerCallbackDynamic(handle, "configReloaded", [&](void *s, SCallbackInfo &i, std::any p) { onConfigReload(); });
    static auto PONMONITORFOCUSCHANGE = HyprlandAPI::registerCallbackDynamic(handle, "focusedMon", [&](void *s, SCallbackInfo &i, std::any p) { onMonitorFocusChange(std::any_cast<PHLMONITOR>(p)); });
    static auto PONMOUSECLICKC = HyprlandAPI::registerCallbackDynamic(handle, "mouseButton", [&](void *s, SCallbackInfo &i, std::any p) { onMouseClick(i, p); });
    static auto PONMOUSEMOVE = HyprlandAPI::registerCallbackDynamic(handle, "mouseMove", [&](void *s, SCallbackInfo &i, std::any p) { onMouseMove(i, p); });

  } catch (const std::exception &e) {
    Log::logger->log(Log::ERR, "Failed to register callbacks: {}", e.what());
    return {PLUGIN_NAME, PLUGIN_DESCRIPTION, PLUGIN_AUTHOR, PLUGIN_VERSION};
  }

  HyprlandAPI::addConfigValue(PHANDLE, "plugin:alttab:font_size", Hyprlang::INT{24});
  HyprlandAPI::addConfigValue(PHANDLE, "plugin:alttab:border_size", Hyprlang::INT{1});
  HyprlandAPI::addConfigValue(PHANDLE, "plugin:alttab:border_rounding", Hyprlang::INT{0});
  HyprlandAPI::addConfigValue(PHANDLE, "plugin:alttab:border_rounding_power", Hyprlang::FLOAT{2});
  HyprlandAPI::addConfigValue(PHANDLE, "plugin:alttab:border_active", Hyprlang::CConfigCustomValueType{&configHandleGradientSet, &configHandleGradientDestroy, "0xff00ccdd"});
  HyprlandAPI::addConfigValue(PHANDLE, "plugin:alttab:border_inactive", Hyprlang::CConfigCustomValueType{&configHandleGradientSet, &configHandleGradientDestroy, "0xaabbccddff"});
  HyprlandAPI::addConfigValue(PHANDLE, "plugin:alttab:window_spacing", Hyprlang::INT{10});
  HyprlandAPI::addConfigValue(PHANDLE, "plugin:alttab:window_size_inactive", Hyprlang::FLOAT{0.8});
  HyprlandAPI::addConfigValue(PHANDLE, "plugin:alttab:monitor_size", Hyprlang::FLOAT{0.3});
  HyprlandAPI::addConfigValue(PHANDLE, "plugin:alttab:animation_speed", Hyprlang::FLOAT{1.0});
  HyprlandAPI::addConfigValue(PHANDLE, "plugin:alttab:unfocused_alpha", Hyprlang::FLOAT{0.6});
  HyprlandAPI::addConfigValue(PHANDLE, "plugin:alttab:include_special", Hyprlang::INT{1});
  HyprlandAPI::addConfigValue(PHANDLE, "plugin:alttab:monitor_spacing", Hyprlang::INT{10});
  HyprlandAPI::addConfigValue(PHANDLE, "plugin:alttab:monitor_size_active", Hyprlang::FLOAT{0.4});
  HyprlandAPI::addConfigValue(PHANDLE, "plugin:alttab:monitor_size_inactive", Hyprlang::FLOAT{0.3});

  g_pCarouselManager = makeUnique<CarouselManager>();

  HyprlandAPI::reloadConfig();

  MONITOR = Desktop::focusState()->monitor();

  try {
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
  } catch (const std::exception &e) {
    Log::logger->log(Log::ERR, "Failed to hook CKeybindManager::onKeyEvent: {}", e.what());
  }

  return {PLUGIN_NAME, PLUGIN_DESCRIPTION, PLUGIN_AUTHOR, PLUGIN_VERSION};
}

APICALL EXPORT void PLUGIN_EXIT() {
  g_pCarouselManager.reset();
  keyhookfn->unhook();
  keyhookfn = nullptr;

  MONITOR = nullptr;
  PHANDLE = nullptr;
}
