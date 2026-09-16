#pragma once

#include <QImage>
#include <QList>
#include <QObject>
#include <QPointF>
#include <QSize>
#include <QString>
#include <QtCore/qnamespace.h>

#include <openvr.h>

// Platform-neutral capture/interaction backend contract. A backend (WGC on Windows, a
// PipeWire/X11 backend on Linux later) provides desktop surfaces, thumbnails, and live
// capture sources. Everything OS-specific lives behind these Qt interfaces; the rest of
// the app (WindowManager, CaptureOverlayScene) depends only on this header. The concrete
// backend is chosen at build time via CMake (see backend_factory.h).
namespace QOverlay::Capture {

// A capturable desktop surface. Opaque `id` round-trips a selection back to the backend;
// native handles stay inside the backend.
struct CaptureSurface {
	enum class Kind { Monitor, Window };

	QString id;
	QString title;    // window title / monitor label
	QString appName;  // owning application (exe/product) for windows; empty for monitors
	Kind kind = Kind::Monitor;

	QString kindString() const { return kind == Kind::Monitor ? QStringLiteral("monitor") : QStringLiteral("window"); }
};

// A live capture of one surface: submits frames straight to an OpenVR overlay in whatever
// texture type the backend uses, and injects pointer input mapped from an overlay-space
// hit. A QObject so backends can signal lifecycle changes.
class ICaptureSource : public QObject {
	Q_OBJECT
public:
	using QObject::QObject;
	~ICaptureSource() override = default;

	// True once the capture is running and can be submitted/interacted with.
	virtual bool isReady() const = 0;

	// Native pixel size of the captured surface (defines the overlay aspect ratio).
	virtual QSize frameSize() const = 0;

	// Pull the latest frame and submit it to `overlay`. Called once per VR tick.
	virtual void submit(vr::VROverlayHandle_t overlay) = 0;

	// Map an overlay-space pixel hit to the real target and inject input. `button ==
	// Qt::NoButton` is hover/move; a button is press/drag. mouseGone() releases any held
	// button when the pointer leaves the overlay.
	virtual void injectMouse(Qt::MouseButton button, const QPointF& overlayPixel) = 0;
	virtual void mouseGone() = 0;

	// Wheel-scroll the target at an overlay-space pixel by `notches` detents (+up / -down).
	// Default no-op so backends that don't yet support scrolling need no change.
	virtual void injectScroll(int notches, const QPointF& overlayPixel) { Q_UNUSED(notches); Q_UNUSED(overlayPixel); }

	// Keyboard injection into this source's target (from the virtual keyboard). Printable
	// input arrives as Unicode text; non-printable keys (Enter, Backspace, arrows, …) as
	// key up/down events.
	virtual void injectText(const QString& text) = 0;
	virtual void injectKey(Qt::Key key, bool down) = 0;

signals:
	void frameSizeChanged(QSize size);
	void targetLost();  // the captured window/monitor disappeared
	void interacted();  // a click landed here — used to focus keyboard input on this target
};

// The platform capture provider: enumerates surfaces, grabs thumbnails, creates sources.
class ICaptureBackend : public QObject {
	Q_OBJECT
public:
	using QObject::QObject;
	~ICaptureBackend() override = default;

	// Human-readable backend name (e.g. "Windows.Graphics.Capture").
	virtual QString name() const = 0;

	// Current monitors + windows. Also refreshes any internal id→target cache.
	virtual QList<CaptureSurface> enumerateSurfaces() = 0;

	// One-shot preview of a surface (from the most recent enumeration), scaled so its
	// longest side is `maxDim`. Null image on failure.
	virtual QImage grabThumbnail(const QString& surfaceId, int maxDim) = 0;

	// The surface's application icon (window/exe icon), at most `maxDim` px. Default null so
	// backends without icon support need no change; the UI falls back to a generic glyph.
	virtual QImage grabIcon(const QString& surfaceId, int maxDim) { Q_UNUSED(surfaceId); Q_UNUSED(maxDim); return {}; }

	// Start a live source for `surfaceId`. Returns nullptr on failure; the caller owns it.
	virtual ICaptureSource* createSource(const QString& surfaceId, QObject* parent = nullptr) = 0;
};

}
