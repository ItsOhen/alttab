#pragma once
#include "logger.hpp"
#include <src/desktop/DesktopTypes.hpp>
#include <src/event/EventBus.hpp>
#include <src/helpers/Color.hpp>
#include <src/plugins/PluginAPI.hpp>

#define HOOK_EVENT(PATH, LAMBDA) Event::bus()->m_events.PATH.listen(LAMBDA);

inline HANDLE PHANDLE = nullptr;

using Timestamp = std::chrono::steady_clock::time_point;
using DeltaTime = std::chrono::duration<long long, std::nano>;
using FloatTime = std::chrono::duration<float>;

#define NOW std::chrono::steady_clock::now()
