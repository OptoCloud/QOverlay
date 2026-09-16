#pragma once

#include "capture/capture_backend.h"
#include "backends/windows/surface_enum.h"
#include "backends/windows/wgc_frame_pool.h"

#include <cstdint>

#include <QSize>

namespace QOverlay::Capture::Win {

// ICaptureSource backed by Windows.Graphics.Capture. Owns a D3D11 device + frame pool and
// submits frames to its OpenVR overlay as a DirectX texture (TextureType_DirectX). Input
// injection maps the overlay-space hit to the live target and drives it:
//  - monitor: global cursor + real clicks via SendInput (screen space is correct);
//  - window:  global cursor move (so WGC's cursor capture renders it) but clicks posted
//    straight to the window's control, so they hit that window regardless of occlusion.
class WgcSource : public ICaptureSource {
	Q_OBJECT
public:
	explicit WgcSource(const CaptureTarget& target, QObject* parent = nullptr);
	~WgcSource() override;

	// Create the D3D device and begin capturing. Returns false on failure.
	bool start();

	bool isReady() const override { return m_device != nullptr && m_pool.Ok(); }
	QSize frameSize() const override { return m_size; }
	void submit(vr::VROverlayHandle_t overlay) override;
	void injectMouse(Qt::MouseButton button, const QPointF& overlayPixel) override;
	void mouseGone() override;
	void injectText(const QString& text) override;
	void injectKey(Qt::Key key, bool down) override;

private:
	bool createDevice();
	// Map an overlay-space pixel to an absolute virtual-desktop pixel for the live target.
	bool overlayPixelToDesktop(const QPointF& pixel, int& outX, int& outY) const;

	CaptureSurface::Kind m_kind = CaptureSurface::Kind::Monitor;
	HMONITOR m_monitor = nullptr;
	HWND m_window = nullptr;

	winrt::com_ptr<ID3D11Device> m_device;
	winrt::com_ptr<ID3D11DeviceContext> m_context;
	WgcFramePool m_pool;
	QSize m_size;

	Qt::MouseButton m_heldButton = Qt::NoButton; // button currently injected as "down"
	HWND m_lastWndTarget = nullptr;              // last window-message target (for release)
	HWND m_downTarget = nullptr;                 // child window that received the button-down
	LPARAM m_lastWndLParam = 0;

	// Click-vs-drag deadzone: while a button is held the cursor is pinned to where the press
	// started until it moves past a threshold, so VR-pointer jitter clicks cleanly instead
	// of registering a tiny drag.
	QPointF m_pressAnchor;
	bool m_dragging = false;

	bool m_loggedSubmit = false;
	std::uint64_t m_submitErrors = 0;
};

}
