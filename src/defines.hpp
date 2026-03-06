#pragma once
#include "logger.hpp"
#include <src/config/ConfigDataValues.hpp>
#include <src/desktop/DesktopTypes.hpp>
#include <src/helpers/Color.hpp>
#include <src/plugins/PluginAPI.hpp>

#ifdef HYPRLAND_LEGACY
#include <src/plugins/HookSystem.hpp>
#else
#include <src/event/EventBus.hpp>
#define HOOK_EVENT(PATH, LAMBDA) Event::bus()->m_events.PATH.listen(LAMBDA);
#endif

inline HANDLE PHANDLE = nullptr;

enum class Direction : uint8_t {
  UP,
  DOWN,
  LEFT,
  RIGHT
};

#define CONFIG_VARS                                                \
  X(INT, fontSize, "font_size", 24)                                \
  X(INT, borderSize, "border_size", 1)                             \
  X(INT, borderRounding, "border_rounding", 0)                     \
  X(FLOAT, borderRoundingPower, "border_rounding_power", 2.0f)     \
  X(INT, dimEnabled, "dim", 1)                                     \
  X(FLOAT, dimAmount, "dim_amount", 0.3f)                          \
  X(INT, blurBG, "blur", 1)                                        \
  X(FLOAT, unfocusedAlpha, "unfocused_alpha", 0.6f)                \
  X(INT, powersave, "powersave", 1)                                \
  X(FLOAT, rotationSpeed, "animation_speed", 1.0f)                 \
  X(FLOAT, carouselSize, "carousel_size", 0.5f)                    \
  X(FLOAT, windowSize, "window_size", 0.3f)                        \
  X(FLOAT, windowSizeActive, "window_size_active", 1.2f)           \
  X(FLOAT, windowSizeInactive, "window_size_inactive", 0.7f)       \
  X(FLOAT, warp, "warp", 0.20f)                                    \
  X(FLOAT, tilt, "tilt", 10.0f)                                    \
  X(INT, bringToActive, "bring_to_active", 1)                      \
  X(INT, splitMonitor, "split_monitor", 1)                         \
  X(FLOAT, monitorSpacing, "monitor_spacing", 0.3f)                \
  X(FLOAT, monitorAnimationSpeed, "monitor_animation_speed", 0.4f) \
  X(INT, grace, "grace", 100)                                      \
  X(INT, includeSpecial, "include_special", 1)                     \
  X(STRING, style, "style", "carousel")

namespace Config {
#define X(type, name, conf, def) inline Hyprlang::type name;
CONFIG_VARS
#undef X

inline CGradientValueData *activeBorderColor = nullptr;
inline CGradientValueData *inactiveBorderColor = nullptr;
} // namespace Config

using Timestamp = std::chrono::steady_clock::time_point;
using DeltaTime = std::chrono::duration<long long, std::nano>;
using FloatTime = std::chrono::duration<float>;

#define NOW std::chrono::steady_clock::now()
