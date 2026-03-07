#pragma once

#include "defines.hpp"
#include <chrono>
#include <src/debug/log/Logger.hpp>
#include <src/desktop/DesktopTypes.hpp>

namespace Log {
enum LogType : uint32_t {
  NONE = 0,
  DRAW = 1 << 0,
  INPUT = 1 << 1,
  SNAPSHOT = 1 << 2,
  ANIMATE = 1 << 3,
  UPDATE = 1 << 4,
  MOVE = 1 << 5,
  MOUSE = 1 << 6,
  ALL = 0xFFFFFFFF
};

#ifdef LOGTYPE
inline uint32_t enabledTypes = LOGTYPE;
#else
inline uint32_t enabledTypes = NONE;
#endif
} // namespace Log

inline void makeTimestamp(char *buf, size_t len) {
  using namespace std::chrono;

  auto now = system_clock::now();
  std::time_t tt = system_clock::to_time_t(now);

  std::tm tm{};
  localtime_r(&tt, &tm);

  auto us =
      duration_cast<microseconds>(now.time_since_epoch()).count() % 1'000'000;

  std::snprintf(buf, len,
                "%02d:%02d:%02d.%06lld",
                tm.tm_hour,
                tm.tm_min,
                tm.tm_sec,
                (long long)us);
}

class ScopeLogger {
public:
  ScopeLogger(const char *fn,
              Log::LogType type = Log::LogType::NONE,
              Hyprutils::CLI::eLogLevel level = Log::ERR)
      : m_fn(fn), m_level(level), m_type(type) {

    m_shouldLog = (m_type == Log::ALL) || (Log::enabledTypes & m_type);

    if (!m_shouldLog)
      return;

    m_start = std::chrono::steady_clock::now();

    char timestamp[32];
    makeTimestamp(timestamp, sizeof(timestamp));

    Log::logger->log(m_level, "[{}] [{}] > Enter {}", timestamp, PLUGIN_NAME, m_fn);
  }

  ~ScopeLogger() {
    if (!m_shouldLog)
      return;

    char timestamp[32];
    makeTimestamp(timestamp, sizeof(timestamp));

    auto duration = std::chrono::steady_clock::now() - m_start;
    char durationBuf[32];
    formatDuration(durationBuf, sizeof(durationBuf), duration);

    Log::logger->log(m_level, "[{}] [{}] < Exit {} ({})", timestamp, PLUGIN_NAME, m_fn, durationBuf);
  }

private:
  static void formatDuration(char *buf,
                             size_t len,
                             std::chrono::steady_clock::duration duration) {
    auto us =
        std::chrono::duration_cast<std::chrono::microseconds>(duration).count();

    if (us >= 1'000'000)
      std::snprintf(buf, len, "%.2fs", us / 1'000'000.0);
    else if (us >= 1'000)
      std::snprintf(buf, len, "%.2fms", us / 1'000.0);
    else
      std::snprintf(buf, len, "%ldus", us);
  }

private:
  const char *m_fn;
  Hyprutils::CLI::eLogLevel m_level;
  std::chrono::steady_clock::time_point m_start;
  Log::LogType m_type;
  bool m_shouldLog = false;
};

class DebugText {
public:
  void add(const std::string &text);
  void draw(PHLMONITOR monitor);

private:
  std::string m_sBuffer;
};

inline UP<DebugText> Overlay = makeUnique<DebugText>();

#ifndef NDEBUG
#define LOG_SCOPE(...) ScopeLogger scope_log(__FUNCTION__ __VA_OPT__(, ) __VA_ARGS__);
#else
#define LOG_SCOPE(...) \
  do {                 \
  } while (0);
#endif

#ifndef NDEBUG
#define LOG(TYPE, FMT, ...)                                             \
  do {                                                                  \
    if ((TYPE == Log::ALL) || (Log::enabledTypes & (TYPE))) {           \
      char _ts[32];                                                     \
      makeTimestamp(_ts, sizeof(_ts));                                  \
      Log::logger->log(Log::ERR,                                        \
                       "[{}] [{}] {}: " FMT,                            \
                       _ts,                                             \
                       PLUGIN_NAME,                                     \
                       __PRETTY_FUNCTION__ __VA_OPT__(, ) __VA_ARGS__); \
    }                                                                   \
  } while (0)
#else
#define LOG(...) \
  do {           \
  } while (0)
#endif
