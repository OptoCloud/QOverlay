#include "log.h"

#include <cstdio>
#include <cstdlib>
#include <exception>

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QMutex>
#include <QStandardPaths>
#include <QString>
#include <QTextStream>
#include <QtGlobal>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#endif

namespace {

QFile* g_file = nullptr;
QTextStream* g_stream = nullptr;
QMutex g_mutex;

// Write one line to file + stderr, flushed immediately so a crash keeps the tail.
void WriteLine(const char* level, const QString& message) {
	const QString line = QDateTime::currentDateTime().toString("HH:mm:ss.zzz")
		+ " [" + level + "] " + message;

	QMutexLocker lock(&g_mutex);
	if (g_stream != nullptr) {
		*g_stream << line << '\n';
		g_stream->flush();
	}
	std::fprintf(stderr, "%s\n", line.toLocal8Bit().constData());
	std::fflush(stderr);
}

const char* LevelName(QOverlay::LogLevel level) {
	switch (level) {
		case QOverlay::LogLevel::Debug:   return "debug";
		case QOverlay::LogLevel::Info:    return "info";
		case QOverlay::LogLevel::Warning: return "warn";
		case QOverlay::LogLevel::Error:   return "error";
	}
	return "info";
}

// Route Qt's own diagnostics (incl. QML engine errors) into the same log.
void ForwardQtMessage(QtMsgType type, const QMessageLogContext& ctx, const QString& msg) {
	const QString cat = ctx.category ? QString("qt/%1").arg(ctx.category) : QStringLiteral("qt");
	const QString text = cat + ": " + msg;
	switch (type) {
		case QtDebugMsg:    WriteLine("debug", text); break;
		case QtInfoMsg:     WriteLine("info", text); break;
		case QtWarningMsg:  WriteLine("warn", text); break;
		case QtCriticalMsg: WriteLine("error", text); break;
		case QtFatalMsg:    WriteLine("fatal", text); break;
	}
}

#ifdef _WIN32
// Log a module+offset backtrace so the crash site is identifiable from the map/PDB even
// without a debugger attached.
void LogBacktrace() {
	void* frames[40];
	const USHORT count = CaptureStackBackTrace(0, 40, frames, nullptr);
	for (USHORT i = 0; i < count; ++i) {
		HMODULE module = nullptr;
		if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
				reinterpret_cast<LPCSTR>(frames[i]), &module) && module != nullptr) {
			char path[MAX_PATH] = {};
			GetModuleFileNameA(module, path, MAX_PATH);
			const QString base = QString::fromLocal8Bit(path).section('\\', -1);
			const quintptr off = reinterpret_cast<quintptr>(frames[i]) - reinterpret_cast<quintptr>(module);
			WriteLine("fatal", QString("  #%1 %2+0x%3").arg(i).arg(base).arg(off, 0, 16));
		} else {
			WriteLine("fatal", QString("  #%1 0x%2").arg(i).arg(reinterpret_cast<quintptr>(frames[i]), 0, 16));
		}
	}
}

LONG WINAPI CrashHandler(EXCEPTION_POINTERS* info) {
	WriteLine("fatal", QString("unhandled exception code=0x%1 at 0x%2")
		.arg(info->ExceptionRecord->ExceptionCode, 0, 16)
		.arg(reinterpret_cast<quintptr>(info->ExceptionRecord->ExceptionAddress), 0, 16));
	LogBacktrace();
	return EXCEPTION_CONTINUE_SEARCH; // let the default handler still produce a crash dump
}
#endif

// Catches uncaught C++ exceptions (std::terminate), which the SEH filter above misses.
void TerminateHandler() {
	QString detail = "std::terminate (uncaught C++ exception)";
	if (std::exception_ptr e = std::current_exception()) {
		try { std::rethrow_exception(e); }
		catch (const std::exception& ex) { detail += QString(": %1").arg(ex.what()); }
		catch (...) { detail += ": (non-std exception)"; }
	}
	WriteLine("fatal", detail);
#ifdef _WIN32
	LogBacktrace();
#endif
	std::abort();
}

}

std::string QOverlay::InitLogging(const std::string& logFileBaseName) {
	QString dir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
	if (dir.isEmpty()) dir = QDir::homePath();
	QDir().mkpath(dir);
	const QString path = QDir(dir).filePath(QString::fromStdString(logFileBaseName) + ".log");

	// Append (don't truncate) so a crash log survives a relaunch — otherwise the next launch
	// wipes the very log we need. A separator marks each session.
	g_file = new QFile(path);
	if (g_file->open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
		g_stream = new QTextStream(g_file);
	} else {
		delete g_file;
		g_file = nullptr;
	}

	qInstallMessageHandler(ForwardQtMessage);
	std::set_terminate(TerminateHandler);
#ifdef _WIN32
	SetUnhandledExceptionFilter(CrashHandler);
#endif

	WriteLine("info", "========== session start ==========");
	WriteLine("info", "logging initialized -> " + path);
	return path.toStdString();
}

void QOverlay::LogWrite(LogLevel level, const std::string& message) {
	WriteLine(LevelName(level), QString::fromStdString(message));
}
