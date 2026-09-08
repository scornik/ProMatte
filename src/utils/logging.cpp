#include "utils/logging.h"

#include <atomic>
#include <cstdio>
#include <cstring>

namespace promatte::log {

namespace {
void defaultSink(Level level, const char *message)
{
	const char *tag = level == Level::Error     ? "ERROR"
			  : level == Level::Warning ? "WARN"
			  : level == Level::Info    ? "INFO"
						    : "DEBUG";
	std::fprintf(stderr, "[promatte][%s] %s\n", tag, message);
}
std::atomic<Sink> g_sink{&defaultSink};
std::atomic<int> g_minLevel{static_cast<int>(Level::Info)};
} // namespace

void setSink(Sink sink)
{
	g_sink.store(sink ? sink : &defaultSink);
}

void setMinLevel(Level level)
{
	g_minLevel.store(static_cast<int>(level));
}

Level minLevel()
{
	return static_cast<Level>(g_minLevel.load());
}

void write(Level level, const char *fmt, ...)
{
	if (static_cast<int>(level) < g_minLevel.load())
		return;
	char buf[2048];
	va_list args;
	va_start(args, fmt);
	std::vsnprintf(buf, sizeof(buf), fmt, args);
	va_end(args);
	buf[sizeof(buf) - 1] = '\0';
	g_sink.load()(level, buf);
}

} // namespace promatte::log
