#pragma once

#include <QObject>
#include <QVariantMap>
#include <QtCore/qnamespace.h>

namespace QOverlay::Capture {

class ICaptureBackend;

// Create the capture backend selected at build time (CMake `QOVERLAY_BACKEND`). The
// implementation lives in exactly one compiled TU: the Windows/WGC backend, or a stub
// that returns nullptr on platforms without a backend yet. Returns nullptr when no
// backend is available; callers must handle that (an empty surface list).
ICaptureBackend* createCaptureBackend(QObject* parent = nullptr);

// Current OS keyboard-layout legends for the on-screen keyboard. Returns a map:
//   { "iso": bool, "k": { "<scancode>": { "n": base char, "s": shift char }, ... } }
// keyed by set-1 scancode (decimal string), so the QML owns the physical arrangement and
// just looks legends up by scancode. `iso` picks the ISO vs ANSI physical layout. Follows
// the active OS layout (ANSI/ISO/AZERTY/…). Empty map when unsupported — the QML keyboard
// then falls back to a static ANSI template. Implemented per backend TU.
QVariantMap currentKeyboardLegends();

// A cheap, opaque token identifying the currently-effective keyboard layout (the foreground
// window's input language). It changes when the user switches language or focuses a window
// with a different layout, so callers can poll it and only re-query the (heavier) legends
// when it actually changed. 0 where there's no platform query yet.
quint64 currentKeyboardLayoutToken();

// Tap a key globally through the OS (down then up), independent of any captured window —
// for shell/system keys like the Windows key that the shell reads from global input, not a
// window message. Implemented per backend TU (no-op where there's no input backend yet).
void injectSystemKey(Qt::Key key);

}
