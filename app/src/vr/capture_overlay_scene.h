#pragma once

#include "vr/ioverlay_scene.h"
#include "capture/capture_backend.h"

#include <memory>

#include <QSize>

namespace QOverlay::VR {

// IOverlayScene adapter over a platform capture source. Backend-neutral: it forwards the
// OpenVR handle, frame submission, and pointer input to an ICaptureSource, so the same
// class serves any capture backend (WGC today, PipeWire/X11 later). Takes ownership of
// the source. Mouse input reaches the real desktop via the source's injector; grab (grip)
// stays handled by the raycaster, exactly like the QML overlays.
class CaptureOverlayScene : public IOverlayScene {
public:
	explicit CaptureOverlayScene(Capture::ICaptureSource* source);
	~CaptureOverlayScene() override;

	bool Ok() const override { return m_source != nullptr; }
	bool Ready() const override {
		return m_source != nullptr && m_handle != vr::k_ulOverlayHandleInvalid && m_source->isReady();
	}
	vr::VROverlayHandle_t Handle() const override { return m_handle; }
	void SetHandle(vr::VROverlayHandle_t handle) override { m_handle = handle; }
	QSize Size() const override { return m_source ? m_source->frameSize() : QSize(); }
	// (m_source is a QObject with no parent, so this scene owns it outright — hence unique_ptr.)

	// Desktop injection is a single OS cursor, so the hand index is ignored.
	bool FireMouseEvent(int hand, Qt::MouseButton button, const glm::vec2& pos) override;
	void MouseNotPresent(int hand) override;

	// Pull the latest captured frame and submit it to the overlay. Call once per VR tick.
	void Submit();

	// Replace the capture source (for retargeting to a different window/screen). Takes
	// ownership of `source` and destroys the previous one.
	void SetSource(Capture::ICaptureSource* source);
	Capture::ICaptureSource* Source() const { return m_source.get(); }

private:
	std::unique_ptr<Capture::ICaptureSource> m_source; // owned
	vr::VROverlayHandle_t m_handle = vr::k_ulOverlayHandleInvalid;
};

}
