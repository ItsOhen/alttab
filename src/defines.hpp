#pragma once
#include "logger.hpp"
#include <src/config/ConfigDataValues.hpp>
#include <src/desktop/DesktopTypes.hpp>
#ifdef HYPRLAND_NEW_EVENTS
#include <src/event/EventBus.hpp>
#define HOOK_EVENT(PATH, LAMBDA) Event::bus()->m_events.PATH.listen(LAMBDA);
#else
#include <src/plugins/HookSystem.hpp>
#endif
#include <src/helpers/Color.hpp>
#include <src/plugins/PluginAPI.hpp>

inline HANDLE PHANDLE = nullptr;

inline bool DIMENABLED = true;
inline bool BLURBG = true;
inline bool POWERSAVE = true;
inline bool INCLUDESPECIAL = true;
inline bool BRINGTOACTIVE = true;

inline int FONTSIZE = 24;
inline int BORDERSIZE = 1;
inline int BORDERROUNDING = 0;
inline float BORDERROUNDINGPOWER = 2;

inline CGradientValueData *ACTIVEBORDERCOLOR = nullptr;
inline CGradientValueData *INACTIVEBORDERCOLOR = nullptr;

inline float CAROUSELSIZE = 0.5f;
inline float WINDOWSIZE = 0.3f;
inline float WINDOWSIZEACTIVE = 1.2f;
inline float WINDOWSIZEINACTIVE = 0.7f;
inline float WARP = 0.20f;
inline float TILT = 10.0f;
inline float DIMAMOUNT = 0.3f;
inline float UNFOCUSEDALPHA = 0.6f;
inline float ROTATIONSPEED = 1.0f;
inline bool SPLITMONITOR = true;
inline float MONITORSPACING = 0.3f;
inline float MONITORANIMATIONSPEED = 0.4f;

using Timestamp = std::chrono::steady_clock::time_point;
using DeltaTime = std::chrono::duration<long long, std::nano>;
using FloatTime = std::chrono::duration<float>;

#define NOW std::chrono::steady_clock::now()
