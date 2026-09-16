#pragma once

#include "capture/capture_backend.h"

#include <vector>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

namespace QOverlay::Capture::Win {

// A concrete Windows capture target: a neutral CaptureSurface plus the native handle the
// backend resolves it to. Handles are only valid for the current enumeration.
struct CaptureTarget {
	CaptureSurface::Kind kind = CaptureSurface::Kind::Monitor;
	QString id;
	QString title;
	QString appName;
	HMONITOR monitor = nullptr;
	HWND window = nullptr;

	CaptureSurface toSurface() const { return CaptureSurface{ id, title, appName, kind }; }
};

// All connected monitors, ordered by EnumDisplayMonitors.
std::vector<CaptureTarget> EnumerateMonitors();

// Top-level, user-facing application windows (the "alt-tab" set): visible, titled,
// un-owned, not tool windows, not DWM-cloaked.
std::vector<CaptureTarget> EnumerateWindows();

// Monitors followed by windows.
std::vector<CaptureTarget> EnumerateTargets();

}
