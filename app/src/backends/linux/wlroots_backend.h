#pragma once

#include "capture/capture_backend.h"
#include "backends/linux/linux_gl_submit.h"

#include <vector>

namespace QOverlay::Capture::Linux {

// wlroots-family backend (Hyprland, Sway, river, …). Uses the wlroots protocol extensions
// DIRECTLY, with no xdg-desktop-portal dependency — the key advantage on compositors where
// a working portal isn't installed:
//   - capture:     zwlr_screencopy_manager_v1 (per-output; wlr-foreign-toplevel for windows)
//   - window list: zwlr_foreign_toplevel_manager_v1
//   - input:       zwlr_virtual_pointer_manager_v1 + zwp_virtual_keyboard_manager_v1
//
// SKELETON: the interface is implemented but the Wayland protocol wiring (registry bind +
// wayland-scanner-generated glue + dmabuf/shm frame import) is TODO — see
// docs/LINUX_BACKENDS.md. Written on Windows; unbuilt.
struct WlrTarget {
	CaptureSurface::Kind kind = CaptureSurface::Kind::Monitor;
	QString id;
	QString title;
	void* output = nullptr;   // wl_output* for monitors
	void* toplevel = nullptr; // zwlr_foreign_toplevel_handle_v1* for windows
	QSize size;
};

class WlrSource : public ICaptureSource {
	Q_OBJECT
public:
	WlrSource(const WlrTarget& target, QObject* parent = nullptr);
	~WlrSource() override;

	bool start();

	bool isReady() const override { return m_started; }
	QSize frameSize() const override { return m_size; }
	void submit(vr::VROverlayHandle_t overlay) override;
	void injectMouse(Qt::MouseButton button, const QPointF& overlayPixel) override;
	void mouseGone() override;
	void injectText(const QString& text) override;
	void injectKey(Qt::Key key, bool down) override;

private:
	WlrTarget m_target;
	QSize m_size;
	GlSubmitter m_submitter;
	bool m_started = false;
	Qt::MouseButton m_held = Qt::NoButton;
};

class WlrootsBackend : public ICaptureBackend {
	Q_OBJECT
public:
	// True on a Wayland session whose compositor advertises the wlr-screencopy protocol.
	static bool isAvailable();

	explicit WlrootsBackend(QObject* parent = nullptr);
	~WlrootsBackend() override;

	QString name() const override { return QStringLiteral("wlroots (screencopy/virtual-input)"); }
	QList<CaptureSurface> enumerateSurfaces() override;
	QImage grabThumbnail(const QString& surfaceId, int maxDim) override;
	ICaptureSource* createSource(const QString& surfaceId, QObject* parent) override;

private:
	std::vector<WlrTarget> m_targets;
};

}
