#pragma once
#include <src/config/values/ConfigValues.hpp>
#include <src/desktop/DesktopTypes.hpp>
#include <src/helpers/MiscFunctions.hpp>
#include <src/helpers/Color.hpp>
#include <src/plugins/PluginAPI.hpp>

#ifdef HYPRLAND_LEGACY
#include <src/plugins/HookSystem.hpp>
#else
#include <src/event/EventBus.hpp>
#define HOOK_EVENT(PATH, LAMBDA) Event::bus()->m_events.PATH.listen(LAMBDA);
#endif

inline HANDLE PHANDLE = nullptr;
inline bool OVERRIDE_WORKSPACE = false;

template<typename TargetType, typename ValuePointer>
auto getVal(const ValuePointer& ptr) {
    auto casted = Hyprutils::Memory::dynamicPointerCast<TargetType>(ptr);
    if (!casted) {
        return decltype(casted->value()){}; 
    }
    return casted->value();
}

#define OPTIONAL_FLOAT_OR(opt_var, fallback_var) \
    ((getVal<Config::Values::CFloatValue>(opt_var) == -1.0f) ? getVal<Config::Values::CFloatValue>(fallback_var) : getVal<Config::Values::CFloatValue>(opt_var))

#define OPTIONAL_INT_OR(opt_var, fallback_var) \
    ((getVal<Config::Values::CFloatValue>(opt_var) == -1.0f) ? fallback_var : (int)getVal<Config::Values::CFloatValue>(opt_var))



enum class Direction : uint8_t {
  UP,
  DOWN,
  LEFT,
  RIGHT
};

#define CONFIG_VARS                                                        \
  X(Int, fontSize, "font_size", 24)                                        \
  X(Int, borderSize, "border_size", 1)                                     \
  X(Int, borderRounding, "border_rounding", 0)                             \
  X(Float, borderRoundingPower, "border_rounding_power", 2.0f)             \
  X(Int, dimEnabled, "dim", 1)                                             \
  X(Float, dimAmount, "dim_amount", 0.3f)                                  \
  X(Int, blurBG, "blur", 1)                                                \
  X(Float, unfocusedAlpha, "unfocused_alpha", 0.6f)                        \
  X(Int, powersave, "powersave", 1)                                        \
  X(Int, livePreview, "live_preview", 1)                                   \
  X(Float, previewCutoff, "preview_cutoff", 0.25f)                         \
  X(Float, rotationSpeed, "animation_speed", 1.0f)                         \
  X(Float, windowSize, "window_size", 0.3f)                                \
  X(Float, windowSizeActive, "window_size_active", 1.2f)                   \
  X(Float, windowSizeInactive, "window_size_inactive", 0.6f)               \
  X(Float, warp, "warp", 0.20f)                                            \
  X(Float, tilt, "tilt", 10.0f)                                            \
  X(Int, bringToActive, "bring_to_active", 1)                              \
  X(Int, splitMonitor, "split_monitor", 1)                                 \
  X(Float, monitorSpacing, "monitor_spacing", 0.3f)                        \
  X(Float, monitorAnimationSpeed, "monitor_animation_speed", 0.4f)         \
  X(Float, monitorFade, "monitor_fade", 0.4f)                              \
  X(Int, grace, "grace", 100)                                              \
  X(Int, includeSpecial, "include_special", 1)                             \
  X(String, style, "style", "carousel")

#define CONFIG_VARS_OPTIONAL_FLOAT              \
  X(Float, carouselSize, "carousel:size")       \
  X(Float, CWSize, "carousel:window_size")      \
  \
  X(Float, CWSizeActive, "carousel:active")     \
  X(Float, CWSizeInactive, "carousel:inactive") \
  X(Float, gridSize, "grid:size")               \
  X(Float, gridColumns, "grid:columns")         \
  X(Float, GWSize, "grid:window_size")          \
  X(Float, GWSizeActive, "grid:active")         \
  X(Float, GWSizeInactive, "grid:inactive")     \
  X(Float, slideSize, "slide:window_size")      \
  X(Float, slideSizeActive, "slide:active")     \
  X(Float, slideSizeInactive, "slide:inactive") \
  X(Float, slideSpacing, "slide:spacing")       \
  X(Float, gridSpacing, "grid:spacing")

namespace Config {
#define X(type, name, conf, def) inline SP<Config::Values::IValue> name;
CONFIG_VARS
#undef X

#define X(type, name, conf) inline SP<Config::Values::IValue> name;
CONFIG_VARS_OPTIONAL_FLOAT
#undef X

inline SP<Config::Values::IValue> activeBorderColor;
inline SP<Config::Values::IValue> inactiveBorderColor;
} // namespace Config

using Timestamp = std::chrono::steady_clock::time_point;
using DeltaTime = std::chrono::duration<long long, std::nano>;
using FloatTime = std::chrono::duration<float>;

#define NOW std::chrono::steady_clock::now()
