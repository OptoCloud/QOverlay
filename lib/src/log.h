#pragma once

#include <string>
#include <utility>

#include <fmt/core.h>

// Logging facade: fmt for formatting, Qt's message handler for the sinks. InitLogging()
// opens a log file (console + file), routes Qt/QML diagnostics into the same file, and
// installs a crash handler so an unhandled fault still leaves a final line. Our code logs
// with the LOG_* macros; Qt's own qDebug/qWarning/QML errors are captured automatically.
namespace QOverlay {

enum class LogLevel { Debug, Info, Warning, Error };

// Level convention: DEBUG for one-shot/verbose diagnostics not needed in normal operation;
// INFO for "operation completed, noting what happened" (e.g. backend selected, overlay
// spawned); WARNING for "completed with degraded capability or a fallback was used" (e.g.
// no backend available, GPU fence timeout); ERROR for "operation failed outright" (e.g.
// overlay creation failed, OpenVR call returned an error). Nothing above ERROR crashes the
// app on its own — Guarded() below is what prevents a thrown exception from doing that.

// Call once, right after the QGuiApplication is constructed. Returns the log file path.
// `logFileBaseName` names the file under Qt's AppLocalDataLocation (which itself is
// already namespaced by QCoreApplication::setApplicationName/setOrganizationName, set
// by the consuming app) - e.g. "qoverlay" -> "qoverlay.log".
std::string InitLogging(const std::string& logFileBaseName = "qoverlay");

void LogWrite(LogLevel level, const std::string& message);

template <typename... Args>
inline void LogFmt(LogLevel level, fmt::format_string<Args...> fmtStr, Args&&... args) {
	LogWrite(level, fmt::format(fmtStr, std::forward<Args>(args)...));
}

// Run `f` catching any exception, so it can't propagate. Use this to wrap the body of any
// method invoked from QML / an event handler — Qt does not support C++ exceptions unwinding
// through its event delivery and crashes if one does. `what` names the site for the log.
//
// Callers pass `[&]() { ... }` — capture-by-reference is safe here because `f` is invoked
// synchronously, inside this call, and never stored or returned; the referenced locals are
// still in scope for the lambda's entire lifetime.
template <typename F>
inline void Guarded(const char* what, F&& f) {
	try {
		f();
	} catch (const std::exception& e) {
		LogWrite(LogLevel::Error, std::string("exception in ") + what + ": " + e.what());
	} catch (...) {
		LogWrite(LogLevel::Error, std::string("unknown exception in ") + what);
	}
}

}

#define LOG_DEBUG(...) ::QOverlay::LogFmt(::QOverlay::LogLevel::Debug, __VA_ARGS__)
#define LOG_INFO(...)  ::QOverlay::LogFmt(::QOverlay::LogLevel::Info, __VA_ARGS__)
#define LOG_WARN(...)  ::QOverlay::LogFmt(::QOverlay::LogLevel::Warning, __VA_ARGS__)
#define LOG_ERROR(...) ::QOverlay::LogFmt(::QOverlay::LogLevel::Error, __VA_ARGS__)
