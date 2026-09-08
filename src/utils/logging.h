#pragma once
// Minimal logging facade. Inside OBS this routes to blog(); in tools/tests it
// prints to stderr. Never throws, safe from any thread.
#include <cstdarg>
#include <string>

namespace promatte::log {

enum class Level { Debug = 0, Info = 1, Warning = 2, Error = 3 };

using Sink = void (*)(Level level, const char *message);

void setSink(Sink sink);
void setMinLevel(Level level);
Level minLevel();

void write(Level level, const char *fmt, ...);

} // namespace promatte::log

#define PM_LOG_DEBUG(...) ::promatte::log::write(::promatte::log::Level::Debug, __VA_ARGS__)
#define PM_LOG_INFO(...) ::promatte::log::write(::promatte::log::Level::Info, __VA_ARGS__)
#define PM_LOG_WARN(...) ::promatte::log::write(::promatte::log::Level::Warning, __VA_ARGS__)
#define PM_LOG_ERROR(...) ::promatte::log::write(::promatte::log::Level::Error, __VA_ARGS__)
