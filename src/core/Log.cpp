#include "wsc/Log.h"

#include <atomic>
#include <cstdio>
#include <mutex>
#include <utility>

namespace wsc {
namespace {

std::atomic<LogLevel> g_level{LogLevel::Warning};
std::mutex g_handlerMutex;
LogHandler g_handler; // empty => default stderr sink

void defaultSink(const LogMessage &message)
{
	std::fprintf(stderr, "[WhatsCanvas][%s][%s] %s\n", Log::levelName(message.level),
	             message.category ? message.category : "", message.message.c_str());
}

} // namespace

void Log::setLevel(LogLevel level)
{
	g_level.store(level, std::memory_order_relaxed);
}

LogLevel Log::level()
{
	return g_level.load(std::memory_order_relaxed);
}

bool Log::isEnabled(LogLevel level)
{
	if (level == LogLevel::Off) {
		return false;
	}
	return static_cast<int>(level) >= static_cast<int>(g_level.load(std::memory_order_relaxed));
}

void Log::setHandler(LogHandler handler)
{
	std::lock_guard<std::mutex> lock(g_handlerMutex);
	g_handler = std::move(handler);
}

void Log::write(LogLevel level, const char *category, std::string message)
{
	if (!isEnabled(level)) {
		return;
	}

	LogHandler handler;
	{
		std::lock_guard<std::mutex> lock(g_handlerMutex);
		handler = g_handler;
	}

	LogMessage record{level, category ? category : "", std::move(message)};
	if (handler) {
		handler(record);
	} else {
		defaultSink(record);
	}
}

const char *Log::levelName(LogLevel level)
{
	switch (level) {
	case LogLevel::Trace:
		return "TRACE";
	case LogLevel::Debug:
		return "DEBUG";
	case LogLevel::Info:
		return "INFO";
	case LogLevel::Warning:
		return "WARN";
	case LogLevel::Error:
		return "ERROR";
	case LogLevel::Off:
		return "OFF";
	}
	return "?";
}

} // namespace wsc
