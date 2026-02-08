#pragma once

#include <hyprlang.hpp>
#include <src/config/ConfigValue.hpp>
#include <src/desktop/DesktopTypes.hpp>
#include <src/helpers/Color.hpp>
#include <src/managers/HookSystemManager.hpp>
inline PHLMONITOR MONITOR = nullptr;
inline HANDLE PHANDLE = nullptr;

inline CConfigValue<Hyprlang::INT> *FONTSIZE;
inline CConfigValue<Hyprlang::INT> *BORDERSIZE;
inline auto BLURENABLED = CConfigValue<Hyprlang::INT>("decoration:blur:enabled");
inline bool DIMENABLED = true;
inline float DIMAMOUNT = 0.15f;
inline CHyprColor DIMCOLOR = CHyprColor(0.0f, 0.0f, 0.0f, DIMAMOUNT);
inline CHyprColor TITLECOLOR = CHyprColor(1.0f, 1.0f, 1.0f, 1.0f);
inline float ANIMATIONSPEED = 0.1f;
