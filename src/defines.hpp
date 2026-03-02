#pragma once
#include "logger.hpp"
#include <src/desktop/DesktopTypes.hpp>
#include <src/event/EventBus.hpp>
#include <src/helpers/Color.hpp>
#include <src/plugins/PluginAPI.hpp>

#define HOOK_EVENT(PATH, LAMBDA) Event::bus()->m_events.PATH.listen(LAMBDA);

inline HANDLE PHANDLE = nullptr;

inline bool DIMENABLED = true;
inline bool BLURBG = true;
inline bool POWERSAVE = true;
inline bool INCLUDESPECIAL = true;

inline float DIMAMOUNT = 0.3f;
inline float UNFOCUSEDALPHA = 0.5f;

using Timestamp = std::chrono::steady_clock::time_point;
using DeltaTime = std::chrono::duration<long long, std::nano>;
using FloatTime = std::chrono::duration<float>;

#define NOW std::chrono::steady_clock::now()
