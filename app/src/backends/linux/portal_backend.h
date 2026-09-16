#pragma once

#include "capture/capture_backend.h"
#include "backends/linux/linux_gl_submit.h"

#include <vector>

namespace QOverlay::Capture::Linux {

// XDG desktop-portal + PipeWire backend for compositors WITHOUT direct capture protocols
// (GNOME, KDE Plasma on Wayland). This is the LAST-RESORT path: it needs a working
// xdg-desktop-portal with a ScreenCast (and, for input, RemoteDesktop) implementation on
// the session bus. Where that's missing — e.g. Cinnamon/Mint Wayland — capture is simply
// unavailable and isAvailable() returns false so the factory can fall back or report it.
//
// Flow:
//   1. org.freedesktop.portal.ScreenCast: CreateSession → SelectSources (monitor|window,
//      the user picks via the compositor's own dialog) → Start → get a PipeWire node id.
//   2. Open the PipeWire remote, connect a stream to the node, negotiate dmabuf/shm.
//   3. org.freedesktop.portal.RemoteDesktop (bound to the same session) for input:
//      NotifyPointerMotionAbsolute / NotifyPointerButton / NotifyKeyboardKeycode.
//
// Note: the portal ScreenCast picker is user-driven — you can't silently enumerate every
// window like X11/wlroots. enumerateSurfaces() therefore returns coarse "pick a screen /
// pick a window" entries; the real target is chosen in the portal dialog on createSource.
//
// SKELETON: interface implemented, D-Bus/PipeWire wiring is TODO (docs/LINUX_BACKENDS.md).

class PortalSource : public ICaptureSource {
	Q_OBJECT
public:
	explicit PortalSource(CaptureSurface::Kind kind, QObject* parent = nullptr);
	~PortalSource() override;

	bool start(); // runs the portal negotiation; false if the user cancels / portal fails

	bool isReady() const override { return m_started; }
	QSize frameSize() const override { return m_size; }
	void submit(vr::VROverlayHandle_t overlay) override;
	void injectMouse(Qt::MouseButton button, const QPointF& overlayPixel) override;
	void mouseGone() override;
	void injectText(const QString& text) override;
	void injectKey(Qt::Key key, bool down) override;

private:
	CaptureSurface::Kind m_kind;
	QSize m_size;
	GlSubmitter m_submitter;
	bool m_started = false;
	Qt::MouseButton m_held = Qt::NoButton;
};

class PortalBackend : public ICaptureBackend {
	Q_OBJECT
public:
	// True only when a ScreenCast portal is actually present on the session bus.
	static bool isAvailable();

	explicit PortalBackend(QObject* parent = nullptr);
	~PortalBackend() override;

	QString name() const override { return QStringLiteral("xdg-desktop-portal + PipeWire"); }
	QList<CaptureSurface> enumerateSurfaces() override;
	QImage grabThumbnail(const QString& surfaceId, int maxDim) override;
	ICaptureSource* createSource(const QString& surfaceId, QObject* parent) override;
};

}
