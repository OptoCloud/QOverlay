#pragma once

#include "capture/capture_backend.h"
#include "backends/linux/linux_gl_submit.h"

#include <vector>

// Avoid pulling Xlib into the header (its macros clash with Qt); use an opaque Display.
typedef struct _XDisplay Display;

namespace QOverlay::Capture::Linux {

// A resolved X11 capture target. `window` is an X Window id (the root window for monitors);
// the rect is the monitor/window geometry in root coordinates.
struct X11Target {
	CaptureSurface::Kind kind = CaptureSurface::Kind::Monitor;
	QString id;
	QString title;
	unsigned long window = 0;
	int x = 0, y = 0, w = 0, h = 0;
};

// X11 capture source: XComposite-redirects a window (so it captures even when occluded),
// or reads a monitor rect from the root, uploads the pixels to a GL texture, and submits
// TextureType_OpenGL. Input is global via XTest (X11 has no reliable per-window inject),
// so window overlays behave like the monitor path — the target should be visible.
class X11Source : public ICaptureSource {
	Q_OBJECT
public:
	X11Source(Display* display, const X11Target& target, QObject* parent = nullptr);
	~X11Source() override;

	bool start();

	bool isReady() const override { return m_started; }
	QSize frameSize() const override { return m_size; }
	void submit(vr::VROverlayHandle_t overlay) override;
	void injectMouse(Qt::MouseButton button, const QPointF& overlayPixel) override;
	void mouseGone() override;
	void injectText(const QString& text) override;
	void injectKey(Qt::Key key, bool down) override;

private:
	bool overlayPixelToRoot(const QPointF& pixel, int& outX, int& outY) const;

	Display* m_display = nullptr;
	X11Target m_target;
	QSize m_size;
	GlSubmitter m_submitter;
	bool m_started = false;
	unsigned long m_pixmap = 0;      // XComposite-named pixmap for window capture
	Qt::MouseButton m_held = Qt::NoButton;
};

// X11 backend: owns the Display connection, enumerates monitors (RandR) and windows
// (_NET_CLIENT_LIST), and creates X11Sources. Requires no desktop portal.
class X11Backend : public ICaptureBackend {
	Q_OBJECT
public:
	// True when an X11 display is reachable ($DISPLAY / XOpenDisplay succeeds).
	static bool isAvailable();

	explicit X11Backend(QObject* parent = nullptr);
	~X11Backend() override;

	bool ok() const { return m_display != nullptr; }

	QString name() const override { return QStringLiteral("X11 (XComposite/XTest)"); }
	QList<CaptureSurface> enumerateSurfaces() override;
	QImage grabThumbnail(const QString& surfaceId, int maxDim) override;
	ICaptureSource* createSource(const QString& surfaceId, QObject* parent) override;

private:
	const X11Target* find(const QString& id) const;

	Display* m_display = nullptr;
	std::vector<X11Target> m_targets;
};

}
