#include "capture/backend_factory.h"

// Built when no platform capture backend is selected (QOVERLAY_BACKEND=stub). Lets the
// app compile and run on platforms without a backend yet — the window manager simply
// shows no capturable surfaces.
QOverlay::Capture::ICaptureBackend* QOverlay::Capture::createCaptureBackend(QObject* /*parent*/) {
	return nullptr;
}

QVariantMap QOverlay::Capture::currentKeyboardLegends() {
	return {}; // no platform layout query — QML uses its static ANSI fallback
}

void QOverlay::Capture::injectSystemKey(Qt::Key /*key*/) {
	// No input backend on this platform build.
}

quint64 QOverlay::Capture::currentKeyboardLayoutToken() {
	return 0; // no platform layout query
}
