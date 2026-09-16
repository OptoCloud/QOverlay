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
