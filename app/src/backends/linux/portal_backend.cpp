#include "backends/linux/portal_backend.h"

#include <cstdlib>

#include <QImage>

#include <fmt/core.h>

// SKELETON. D-Bus (xdg-desktop-portal) + PipeWire wiring is documented and stubbed; see
// docs/LINUX_BACKENDS.md. Compiled only when this backend is selected with its deps present.

using QOverlay::Capture::CaptureSurface;
using QOverlay::Capture::Linux::PortalBackend;
using QOverlay::Capture::Linux::PortalSource;

// ── PortalSource ────────────────────────────────────────────────────────────────────

PortalSource::PortalSource(CaptureSurface::Kind kind, QObject* parent)
	: ICaptureSource(parent)
	, m_kind(kind)
{
}

PortalSource::~PortalSource() = default;

bool PortalSource::start() {
	// TODO(linux): ScreenCast CreateSession/SelectSources/Start over D-Bus, then open the
	// PipeWire node and negotiate a format. m_size comes from the negotiated stream.
	fmt::print("PortalSource: start() is a skeleton — portal/PipeWire negotiation TODO\n");
	m_started = false;
	return m_started;
}

void PortalSource::submit(vr::VROverlayHandle_t overlay) {
	if (!m_started) return;
	// TODO(linux): dequeue the latest PipeWire buffer; dmabuf → EGLImage → GL texture →
	// m_submitter.SubmitTexture(overlay, tex, m_size); or shm → UploadAndSubmit.
	(void)overlay;
}

void PortalSource::injectMouse(Qt::MouseButton button, const QPointF& overlayPixel) {
	if (button != Qt::NoButton && m_held == Qt::NoButton) emit interacted();
	m_held = button;
	// TODO(linux): RemoteDesktop NotifyPointerMotionAbsolute(stream, x, y) + NotifyPointerButton.
	(void)overlayPixel;
}

void PortalSource::mouseGone() {
	// TODO(linux): release held button via NotifyPointerButton.
	m_held = Qt::NoButton;
}

void PortalSource::injectText(const QString& text) {
	// TODO(linux): RemoteDesktop NotifyKeyboardKeysym per character.
	(void)text;
}

void PortalSource::injectKey(Qt::Key key, bool down) {
	// TODO(linux): NotifyKeyboardKeycode with the evdev code for `key`.
	(void)key; (void)down;
}

// ── PortalBackend ───────────────────────────────────────────────────────────────────

bool PortalBackend::isAvailable() {
	const char* wayland = std::getenv("WAYLAND_DISPLAY");
	if (wayland == nullptr || wayland[0] == '\0') return false;
	// TODO(linux): probe the session bus for org.freedesktop.portal.Desktop implementing
	// the ScreenCast interface (and RemoteDesktop for input). Return false when absent —
	// this is the Cinnamon/Mint-Wayland case where capture genuinely isn't possible.
	return true;
}

PortalBackend::PortalBackend(QObject* parent)
	: ICaptureBackend(parent)
{
}

PortalBackend::~PortalBackend() = default;

QList<CaptureSurface> PortalBackend::enumerateSurfaces() {
	// The portal picks the target via its own dialog, so we can only offer coarse choices;
	// the concrete monitor/window is chosen when createSource() runs the portal flow.
	QList<CaptureSurface> out;
	out.append(CaptureSurface{ QStringLiteral("portal:monitor"), QStringLiteral("Pick a screen…"), CaptureSurface::Kind::Monitor });
	out.append(CaptureSurface{ QStringLiteral("portal:window"),  QStringLiteral("Pick a window…"),  CaptureSurface::Kind::Window });
	return out;
}

QImage PortalBackend::grabThumbnail(const QString& /*surfaceId*/, int /*maxDim*/) {
	// No pre-capture preview — the portal hasn't granted access yet.
	return {};
}

QOverlay::Capture::ICaptureSource* PortalBackend::createSource(const QString& surfaceId, QObject* parent) {
	const auto kind = (surfaceId == QLatin1String("portal:window"))
		? CaptureSurface::Kind::Window : CaptureSurface::Kind::Monitor;
	auto* src = new PortalSource(kind, parent);
	if (!src->start()) { delete src; return nullptr; }
	return src;
}
