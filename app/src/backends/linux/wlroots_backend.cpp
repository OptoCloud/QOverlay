#include "backends/linux/wlroots_backend.h"

#include <cstdlib>

#include <QImage>

#include <fmt/core.h>

// SKELETON. The wlroots protocol wiring is documented here and stubbed. Bring-up steps and
// dependencies are in docs/LINUX_BACKENDS.md. This file compiles only when the wlroots
// backend is selected AND its deps are present (CMake gates it); otherwise it's excluded.

using QOverlay::Capture::Linux::WlrSource;
using QOverlay::Capture::Linux::WlrootsBackend;

// ── WlrSource ───────────────────────────────────────────────────────────────────────

WlrSource::WlrSource(const WlrTarget& target, QObject* parent)
	: ICaptureSource(parent)
	, m_target(target)
	, m_size(target.size)
{
}

WlrSource::~WlrSource() = default;

bool WlrSource::start() {
	// TODO(linux): For a monitor, create a zwlr_screencopy_frame from the output and start
	// a copy loop (copy_with_damage). For a window, capture via its foreign-toplevel handle
	// on compositors that support toplevel screencopy (Hyprland does through hyprland-
	// toplevel-export-v1; plain wlroots may not). Import frame buffers (shm or dmabuf) for
	// GL submission.
	m_started = !m_size.isEmpty();
	if (m_started) emit frameSizeChanged(m_size);
	fmt::print("WlrSource: start() is a skeleton — no frames yet (see docs/LINUX_BACKENDS.md)\n");
	return m_started;
}

void WlrSource::submit(vr::VROverlayHandle_t overlay) {
	if (!m_started) return;
	// TODO(linux): request the next screencopy frame; on ready, import its dmabuf as an
	// EGLImage → GL texture and m_submitter.SubmitTexture(overlay, tex, m_size). For shm
	// frames, m_submitter.UploadAndSubmit(overlay, pixels, m_size, bgra).
	(void)overlay;
}

void WlrSource::injectMouse(Qt::MouseButton button, const QPointF& overlayPixel) {
	if (button != Qt::NoButton && m_held == Qt::NoButton) emit interacted();
	m_held = button;
	// TODO(linux): zwlr_virtual_pointer_v1 — motion_absolute(x,y,extent) then button(). This
	// is global input (like X11/monitor), so the target should be visible.
	(void)overlayPixel;
}

void WlrSource::mouseGone() {
	// TODO(linux): release any held virtual-pointer button.
	m_held = Qt::NoButton;
}

void WlrSource::injectText(const QString& text) {
	// TODO(linux): zwp_virtual_keyboard_v1 — upload a temporary keymap and emit key events
	// for each character (or use the compositor's text-input where available).
	(void)text;
}

void WlrSource::injectKey(Qt::Key key, bool down) {
	// TODO(linux): map Qt::Key → evdev keycode and send via zwp_virtual_keyboard_v1.
	(void)key; (void)down;
}

// ── WlrootsBackend ──────────────────────────────────────────────────────────────────

bool WlrootsBackend::isAvailable() {
	// Must be a Wayland session; a full probe binds the registry and checks for
	// zwlr_screencopy_manager_v1 (done at construction). Env check is the cheap gate.
	const char* wayland = std::getenv("WAYLAND_DISPLAY");
	if (wayland == nullptr || wayland[0] == '\0') return false;
	// TODO(linux): open a wl_display, roundtrip the registry, return true only if
	// zwlr_screencopy_manager_v1 is advertised. Env presence is a placeholder.
	return true;
}

WlrootsBackend::WlrootsBackend(QObject* parent)
	: ICaptureBackend(parent)
{
	// TODO(linux): connect to wl_display, bind wl_registry, and cache the wlr managers
	// (screencopy, foreign-toplevel, virtual-pointer, virtual-keyboard) + wl_outputs.
}

WlrootsBackend::~WlrootsBackend() = default;

QList<QOverlay::Capture::CaptureSurface> WlrootsBackend::enumerateSurfaces() {
	m_targets.clear();
	// TODO(linux): one CaptureSurface per wl_output (monitor) and per foreign-toplevel
	// handle (window). Populate m_targets with the wl_output*/toplevel handle.
	QList<CaptureSurface> out;
	for (const auto& t : m_targets) out.append(CaptureSurface{ t.id, t.title, t.kind });
	return out;
}

QImage WlrootsBackend::grabThumbnail(const QString& /*surfaceId*/, int /*maxDim*/) {
	// TODO(linux): one-shot screencopy of the target scaled down. Empty for now.
	return {};
}

QOverlay::Capture::ICaptureSource* WlrootsBackend::createSource(const QString& surfaceId, QObject* parent) {
	for (const auto& t : m_targets) {
		if (t.id == surfaceId) {
			auto* src = new WlrSource(t, parent);
			if (!src->start()) { delete src; return nullptr; }
			return src;
		}
	}
	return nullptr;
}
