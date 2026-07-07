#pragma once

#include <functional>
#include <string>

#include "Export.h"

namespace wsc {

/// Severity levels for diagnostic messages, ordered from most to least verbose.
enum class LogLevel
{
	Trace = 0,   ///< Very fine-grained tracing, usually off.
	Debug = 1,   ///< Developer-facing debug detail.
	Info = 2,    ///< Informational lifecycle messages.
	Warning = 3, ///< Recoverable problems and suspicious usage.
	Error = 4,   ///< Failures that abort an operation.
	Off = 5,     ///< Disable all logging (only valid for setLevel/threshold).
};

/// A single diagnostic record delivered to the active log handler.
struct LogMessage
{
	LogLevel level;         ///< Severity of this message.
	const char *category;   ///< Short static subsystem tag (never null), e.g. "Renderer".
	std::string message;    ///< Human-readable message text.
};

/// Sink that receives log messages. Install one with `Log::setHandler`.
using LogHandler = std::function<void(const LogMessage &)>;

/// Central diagnostics/logging facility for WhatsCanvas. Thread-safe.
///
/// By default only `Warning` and `Error` messages are emitted, and they are
/// written to `stderr`. Applications can lower the threshold with `setLevel`
/// (e.g. to `LogLevel::Debug`) to investigate rendering problems, or route
/// every message into their own logging system with `setHandler`.
class WSC_API Log
{
public:
	/// Set the minimum severity that is emitted. Messages below this level are
	/// dropped cheaply before the handler runs. Use `LogLevel::Off` to silence
	/// all output.
	static void setLevel(LogLevel level);

	/// Current minimum severity threshold.
	static LogLevel level();

	/// Whether a message at `level` would currently be emitted.
	static bool isEnabled(LogLevel level);

	/// Install a custom sink. Pass `nullptr` to restore the default handler,
	/// which writes formatted messages to `stderr`.
	static void setHandler(LogHandler handler);

	/// Emit a diagnostic message. Filtered against the current level first, so
	/// callers may pass an already-built string; for hot paths prefer guarding
	/// with `isEnabled`.
	static void write(LogLevel level, const char *category, std::string message);

	/// Human-readable name of a level, e.g. "ERROR". Never null.
	static const char *levelName(LogLevel level);
};

} // namespace wsc
