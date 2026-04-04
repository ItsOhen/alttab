#include "defines.hpp"
#include "manager.hpp"
#include <hyprutils/memory/UniquePtr.hpp>
#include <src/desktop/state/FocusState.hpp>
#include <src/managers/input/InputManager.hpp>
#ifdef HYPRLAND_LEGACY
#include <src/plugins/HookSystem.hpp>
#endif
#include <src/render/Renderer.hpp>

// void renderWorkspace(PHLMONITOR pMonitor, PHLWORKSPACE pWorkspace, const Time::steady_tp& now, const CBox& geometry)
CFunctionHook *workspacehookfn = nullptr;
typedef void (*CWorkspaceManager_renderWorkspace)(void *self, PHLMONITOR pMonitor, PHLWORKSPACE pWorkspace, const Time::steady_tp &now, const CBox &geometry);

void renderWorkspace(void *self, PHLMONITOR pMonitor, PHLWORKSPACE pWorkspace, const Time::steady_tp &now, const CBox &geometry) {
  if (!manager->isActive() || !OVERRIDE_WORKSPACE)
    ((CWorkspaceManager_renderWorkspace)workspacehookfn->m_original)(self, pMonitor, pWorkspace, now, geometry);
}

CFunctionHook *keyhookfn = nullptr;
typedef bool (*CKeybindManager_onKeyEvent)(void *self, std::any &event, SP<IKeyboard> pKeyboard);

static bool onKeyEvent(void *self, std::any event, SP<IKeyboard> pKeyboard) {
  if (!keyhookfn || !keyhookfn->m_original)
    return true;

  auto e = std::any_cast<IKeyboard::SKeyEvent>(event);
  const auto MODS = g_pInputManager->getModsFromAllKBs();

  if (!manager->isActive() && e.state == WL_KEYBOARD_KEY_STATE_PRESSED) {
    if (e.keycode == 15 && (MODS & HL_MODIFIER_ALT)) {
      manager->activate();
      return false;
    }
  }

  if (!manager->isActive())
    return ((CKeybindManager_onKeyEvent)keyhookfn->m_original)(self, event, pKeyboard);

  const auto KEYSYM = xkb_state_key_get_one_sym(pKeyboard->m_xkbState, e.keycode + 8);

  if (e.state == WL_KEYBOARD_KEY_STATE_PRESSED) {
    switch (KEYSYM) {
    case XKB_KEY_Tab:
    case XKB_KEY_ISO_Left_Tab:
    case XKB_KEY_d:
    case XKB_KEY_Right:
      manager->move((MODS & HL_MODIFIER_SHIFT) ? Direction::LEFT : Direction::RIGHT);
      break;

    case XKB_KEY_Down:
    case XKB_KEY_s:
      manager->move(Direction::DOWN);
      break;

    case XKB_KEY_a:
    case XKB_KEY_Left:
      manager->move((MODS & HL_MODIFIER_SHIFT) ? Direction::RIGHT : Direction::LEFT);
      break;

    case XKB_KEY_w:
    case XKB_KEY_Up:
      manager->move(Direction::UP);
      break;

    case XKB_KEY_Return:
    case XKB_KEY_space:
      manager->confirm();
      break;

    case XKB_KEY_Escape:
      manager->deactivate();
      break;
    }
  } else {
    if (KEYSYM == XKB_KEY_Alt_L || KEYSYM == XKB_KEY_Alt_R || KEYSYM == XKB_KEY_Super_L) {
      manager->confirm();
    }
  }

  return false;
}

// Straight from ConfigManager.cpp. THANKS GUYS!
inline Hyprlang::CParseResult configHandleGradientSet(const char *VALUE, void **data) {
  // if (unloadGuard)
  //   return {};
  std::string V = VALUE;

  if (!*data)
    *data = new CGradientValueData();

  const auto DATA = sc<CGradientValueData *>(*data);

  Hyprutils::String::CVarList2 varlist(std::string(V), 0, ' ');
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

inline void configHandleGradientDestroy(void **data) {
  // if (unloadGuard)
  //   return;
  if (*data)
    delete sc<CGradientValueData *>(*data);
}

void registerConfig() {
#define X(type, name, conf, def) \
  HyprlandAPI::addConfigValue(PHANDLE, "plugin:alttab:" conf, Hyprlang::type{def});
  CONFIG_VARS
#undef X

#define X(type, name, conf) \
  HyprlandAPI::addConfigValue(PHANDLE, "plugin:alttab:" conf, Hyprlang::type{-1});
  CONFIG_VARS_OPTIONAL_FLOAT
#undef X

  HyprlandAPI::addConfigValue(PHANDLE, "plugin:alttab:border_active", Hyprlang::CConfigCustomValueType{&configHandleGradientSet, &configHandleGradientDestroy, "0xff00ccdd"});
  HyprlandAPI::addConfigValue(PHANDLE, "plugin:alttab:border_inactive", Hyprlang::CConfigCustomValueType{&configHandleGradientSet, &configHandleGradientDestroy, "0xaabbccddff"});
}

APICALL EXPORT std::string PLUGIN_API_VERSION() {
  return HYPRLAND_API_VERSION;
}
APICALL EXPORT PLUGIN_DESCRIPTION_INFO PLUGIN_INIT(HANDLE handle) {
  PHANDLE = handle;
  if (const std::string hash = __hyprland_api_get_hash(); hash != __hyprland_api_get_client_hash())
    throw std::runtime_error("Version mismatch");

  manager = makeUnique<alttab::Manager>();
  /* Maybe later.
    HyprlandAPI::addDispatcherV2(PHANDLE, "alttab", [&](std::string args) -> SDispatchResult {
      LOG_SCOPE()
      manager->toggle();
      g_pHyprRenderer->damageMonitor(Desktop::focusState()->monitor());
      return {};
    });

    HyprlandAPI::addDispatcherV2(PHANDLE, "alttab-next", [&](std::string args) -> SDispatchResult {
      if (manager->active)
        manager->next();
      return {};
    });
    HyprlandAPI::addDispatcherV2(PHANDLE, "alttab-prev", [&](std::string args) -> SDispatchResult {
      if (manager->active)
        manager->prev();
      return {};
    });
    */

  registerConfig();

  try {
    auto findAndHook = [&](const std::string &fn, const std::string &match, void *hookFn) {
      auto lookup = HyprlandAPI::findFunctionsByName(PHANDLE, fn);
      SFunctionMatch *fnMatch = nullptr;

      if (lookup.size() != 1) {
        for (auto &f : lookup) {
          Log::logger->log(Log::ERR, "{} candidate at {}\nsig: {}\ndemangled: {}", fn, f.address, f.signature, f.demangled);
          if (match.empty() || f.demangled.find(match) != std::string::npos) {
            fnMatch = &f;
            break;
          }
        }
        if (!fnMatch)
          throw std::runtime_error(fn + " not found");
      } else {
        fnMatch = &lookup[0];
      }

      auto hook = HyprlandAPI::createFunctionHook(PHANDLE, fnMatch->address, hookFn);
      if (!hook->hook())
        throw std::runtime_error("Failed to hook " + fn);

      return hook;
    };

    workspacehookfn = findAndHook("renderWorkspace", "IHyprRenderer::renderWorkspace(", (void *)renderWorkspace);
    keyhookfn = findAndHook("onKeyEvent", "", (void *)onKeyEvent);

  } catch (const std::exception &e) {
    Log::logger->log(Log::ERR, "{}", e.what());
  }

  return {PLUGIN_NAME, PLUGIN_DESCRIPTION, PLUGIN_AUTHOR, PLUGIN_VERSION};
}

APICALL EXPORT void PLUGIN_EXIT() {
  keyhookfn = nullptr;
  workspacehookfn = nullptr;
  manager.reset();
}
