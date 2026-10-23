#include "capture/backend_factory.h"
#include "capture/capture_backend.h"

#include "log.h"

#include <cstdlib>

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

	// Try one backend: log + construct if isAvailable(), else return nullptr so the caller
	// falls through to the next candidate. Collapses the repeated "if (isAvailable()) { log;
	// return new Backend(parent); }" shape below into one call site per backend.
	auto tryBackend = [parent](bool available, const char* logMessage, auto makeBackend) -> ICaptureBackend* {
		if (!available) return nullptr;
		LOG_INFO("QOverlay capture backend: {}", logMessage);
		return makeBackend(parent);
	};

	if (onWayland) {
#ifdef QOVERLAY_HAVE_WLROOTS
		if (auto* b = tryBackend(Linux::WlrootsBackend::isAvailable(), "wlroots (direct, no portal)",
				[](QObject* p) -> ICaptureBackend* { return new Linux::WlrootsBackend(p); })) {
			return b;
		}
#endif
#ifdef QOVERLAY_HAVE_PORTAL
		if (auto* b = tryBackend(Linux::PortalBackend::isAvailable(), "xdg-desktop-portal + PipeWire",
				[](QObject* p) -> ICaptureBackend* { return new Linux::PortalBackend(p); })) {
			return b;
		}
#endif
		LOG_ERROR("No Wayland capture path: compositor advertises no wlr-screencopy and no "
		           "usable xdg ScreenCast portal is present. Capture unavailable.");
		return nullptr;
	}

#ifdef QOVERLAY_HAVE_X11
	if (auto* b = tryBackend(Linux::X11Backend::isAvailable(), "X11 (direct, no portal)",
			[](QObject* p) -> ICaptureBackend* { return new Linux::X11Backend(p); })) {
		return b;
	}
#endif

	LOG_ERROR("No usable capture backend (XDG_SESSION_TYPE={}).", sessionType ? sessionType : "unknown");
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
