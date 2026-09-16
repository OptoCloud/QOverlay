#include "capture/backend_factory.h"
#include "capture/capture_backend.h"

#include <cstdlib>

#include <fmt/core.h>

#ifdef QOVERLAY_HAVE_X11
#include "backends/linux/x11_backend.h"
#endif
#ifdef QOVERLAY_HAVE_WLROOTS
#include "backends/linux/wlroots_backend.h"
#endif
#ifdef QOVERLAY_HAVE_PORTAL
#include "backends/linux/portal_backend.h"
#endif

// Runtime backend chooser for Linux. The whole point: DON'T assume the xdg portal exists.
// Prefer direct, portal-free paths and fall back in order, probing isAvailable() so a
// compiled-in backend that can't actually run on this session is skipped:
//   Wayland: wlroots (direct) → xdg portal (only if a ScreenCast portal is really present)
//   X11:     X11 (direct)
// If nothing works (e.g. Cinnamon/Mint on Wayland with no portal), return null and say so.
QOverlay::Capture::ICaptureBackend* QOverlay::Capture::createCaptureBackend(QObject* parent) {
	const char* sessionType = std::getenv("XDG_SESSION_TYPE");
	const char* waylandDisplay = std::getenv("WAYLAND_DISPLAY");
	const bool onWayland = (waylandDisplay != nullptr && waylandDisplay[0] != '\0');

	if (onWayland) {
#ifdef QOVERLAY_HAVE_WLROOTS
		if (Linux::WlrootsBackend::isAvailable()) {
			fmt::print("QOverlay capture backend: wlroots (direct, no portal)\n");
			return new Linux::WlrootsBackend(parent);
		}
#endif
#ifdef QOVERLAY_HAVE_PORTAL
		if (Linux::PortalBackend::isAvailable()) {
			fmt::print("QOverlay capture backend: xdg-desktop-portal + PipeWire\n");
			return new Linux::PortalBackend(parent);
		}
#endif
		fmt::print("No Wayland capture path: compositor advertises no wlr-screencopy and no "
		           "usable xdg ScreenCast portal is present. Capture unavailable.\n");
		return nullptr;
	}

#ifdef QOVERLAY_HAVE_X11
	if (Linux::X11Backend::isAvailable()) {
		fmt::print("QOverlay capture backend: X11 (direct, no portal)\n");
		return new Linux::X11Backend(parent);
	}
#endif

	fmt::print("No usable capture backend (XDG_SESSION_TYPE={}).\n", sessionType ? sessionType : "unknown");
	return nullptr;
}

QVariantMap QOverlay::Capture::currentKeyboardLegends() {
	// TODO(linux): derive from the active XKB layout (xkb_keymap / xkb_state_key_get_utf8).
	// Empty for now — the QML keyboard falls back to a static ANSI template.
	return {};
}

void QOverlay::Capture::injectSystemKey(Qt::Key /*key*/) {
	// TODO(linux): route the Super key through the active input backend (XTest/uinput).
}

quint64 QOverlay::Capture::currentKeyboardLayoutToken() {
	return 0; // TODO(linux): derive from the active XKB group
}
