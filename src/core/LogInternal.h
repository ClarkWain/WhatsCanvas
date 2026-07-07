#pragma once

// Internal logging helpers built on top of the public wsc::Log facility.
// These macros avoid building the message string when the level is disabled,
// and provide a std::ostream-style syntax for call sites.

#include <sstream>

#include "wsc/Log.h"

#define WSC_LOG(level, category, expr)                                          \
	do {                                                                        \
		if (::wsc::Log::isEnabled(level)) {                                     \
			std::ostringstream wsc_log_oss_;                                    \
			wsc_log_oss_ << expr;                                               \
			::wsc::Log::write((level), (category), wsc_log_oss_.str());         \
		}                                                                       \
	} while (0)

#define WSC_LOG_ERROR(category, expr) WSC_LOG(::wsc::LogLevel::Error, category, expr)
#define WSC_LOG_WARN(category, expr) WSC_LOG(::wsc::LogLevel::Warning, category, expr)
#define WSC_LOG_INFO(category, expr) WSC_LOG(::wsc::LogLevel::Info, category, expr)
#define WSC_LOG_DEBUG(category, expr) WSC_LOG(::wsc::LogLevel::Debug, category, expr)
#define WSC_LOG_TRACE(category, expr) WSC_LOG(::wsc::LogLevel::Trace, category, expr)
