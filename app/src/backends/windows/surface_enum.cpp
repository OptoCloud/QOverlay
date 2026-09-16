#include "backends/windows/surface_enum.h"

#include <iterator>

#include <dwmapi.h>

namespace {

using QOverlay::Capture::CaptureSurface;
using QOverlay::Capture::Win::CaptureTarget;

QString HandleId(const char* prefix, void* handle) {
	return QString("%1:0x%2").arg(prefix).arg(reinterpret_cast<quintptr>(handle), 16, 16, QChar('0'));
}

BOOL CALLBACK MonitorProc(HMONITOR monitor, HDC /*hdc*/, LPRECT /*rect*/, LPARAM param) {
	auto* out = reinterpret_cast<std::vector<CaptureTarget>*>(param);

	MONITORINFOEXW info{};
	info.cbSize = sizeof(info);
	if (!GetMonitorInfoW(monitor, &info)) return TRUE;

	const int w = info.rcMonitor.right - info.rcMonitor.left;
	const int h = info.rcMonitor.bottom - info.rcMonitor.top;
	const int index = static_cast<int>(out->size());
	const bool primary = (info.dwFlags & MONITORINFOF_PRIMARY) != 0;

	CaptureTarget target;
	target.kind = CaptureSurface::Kind::Monitor;
	target.id = QString("monitor:%1").arg(index);
	target.title = QString("Display %1%2 (%3x%4)")
		.arg(index + 1)
		.arg(primary ? " (Primary)" : "")
		.arg(w)
		.arg(h);
	target.monitor = monitor;
	out->push_back(target);
	return TRUE;
}

// The "alt-tab window" heuristic: a real, user-facing top-level window.
bool IsCapturableWindow(HWND hwnd) {
	if (!IsWindowVisible(hwnd)) return false;
	if (hwnd == GetShellWindow()) return false;
	if (GetWindowTextLengthW(hwnd) == 0) return false;

	// Owned windows / popups aren't standalone targets.
	if (GetAncestor(hwnd, GA_ROOTOWNER) != hwnd) return false;

	const LONG exStyle = GetWindowLongW(hwnd, GWL_EXSTYLE);
	if (exStyle & WS_EX_TOOLWINDOW) return false;

	// UWP/other apps park invisible windows off-screen as DWM-cloaked; skip them.
	DWORD cloaked = 0;
	if (SUCCEEDED(DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) && cloaked != 0) {
		return false;
	}

	RECT rect{};
	if (!GetWindowRect(hwnd, &rect)) return false;
	if ((rect.right - rect.left) < 2 || (rect.bottom - rect.top) < 2) return false;

	return true;
}

BOOL CALLBACK WindowProc(HWND hwnd, LPARAM param) {
	if (!IsCapturableWindow(hwnd)) return TRUE;

	auto* out = reinterpret_cast<std::vector<CaptureTarget>*>(param);

	wchar_t buffer[256] = {};
	GetWindowTextW(hwnd, buffer, static_cast<int>(std::size(buffer)));

	CaptureTarget target;
	target.kind = CaptureSurface::Kind::Window;
	target.id = HandleId("window", hwnd);
	target.title = QString::fromWCharArray(buffer);
	target.window = hwnd;
	out->push_back(target);
	return TRUE;
}

}

std::vector<QOverlay::Capture::Win::CaptureTarget> QOverlay::Capture::Win::EnumerateMonitors() {
	std::vector<CaptureTarget> monitors;
	EnumDisplayMonitors(nullptr, nullptr, MonitorProc, reinterpret_cast<LPARAM>(&monitors));
	return monitors;
}

std::vector<QOverlay::Capture::Win::CaptureTarget> QOverlay::Capture::Win::EnumerateWindows() {
	std::vector<CaptureTarget> windows;
	EnumWindows(WindowProc, reinterpret_cast<LPARAM>(&windows));
	return windows;
}

std::vector<QOverlay::Capture::Win::CaptureTarget> QOverlay::Capture::Win::EnumerateTargets() {
	std::vector<CaptureTarget> all = EnumerateMonitors();
	std::vector<CaptureTarget> windows = EnumerateWindows();
	all.insert(all.end(), windows.begin(), windows.end());
	return all;
}
