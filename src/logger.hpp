#pragma once

#include <chrono>
#include <src/debug/log/Logger.hpp>

class ScopeLogger {
public:
  ScopeLogger(const char *fn, Hyprutils::CLI::eLogLevel level = Log::TRACE)
      : m_fn(fn), m_level(level) {
    m_start = std::chrono::steady_clock::now();
    Log::logger->log(m_level, "[{}] > Enter {}", PLUGIN_NAME, m_fn);
  }

  static void formatDuration(char *buf, size_t len, std::chrono::steady_clock::duration duration) {
    auto us = std::chrono::duration_cast<std::chrono::microseconds>(duration).count();

    if (us >= 1000000) {
      snprintf(buf, len, "%.2fs", us / 1000000.0);
    } else if (us >= 1000) {
      snprintf(buf, len, "%.2fms", us / 1000.0);
    } else {
      snprintf(buf, len, "%ldus", us);
    }
  }

  ~ScopeLogger() {
    char timeBuf[32];
    formatDuration(timeBuf, sizeof(timeBuf), std::chrono::steady_clock::now() - m_start);
    Log::logger->log(m_level, "[{}] < Exit {} ({})", PLUGIN_NAME, m_fn, timeBuf);
  }

private:
  const char *m_fn;
  Hyprutils::CLI::eLogLevel m_level;
  std::chrono::steady_clock::time_point m_start;
};

#ifndef NDEBUG
#define LOG_SCOPE(...) ScopeLogger scope_log(__FUNCTION__ __VA_OPT__(, ) __VA_ARGS__);
#else
#define LOG_SCOPE(...) \
  do {                 \
  } while (0);
#endif

#ifndef NDEBUG
#define LOG(LEVEL, FMT, ...) \
  Log::logger->log(Log::LEVEL, "[{}] {}: " FMT, PLUGIN_NAME, __PRETTY_FUNCTION__ __VA_OPT__(, ) __VA_ARGS__)
#else
#define LOG(LEVEL, ...) \
  do {                  \
  } while (0)
#endif
