#pragma once

#include "vr/ioverlay_scene.h"

#include <QUrl>

#include <glm/mat4x4.hpp>
#include <openvr.h>

namespace QOverlay {
class OverlayControls;
class WindowManager;
}

namespace QOverlay::VR {

class Overlay;
class QmlOverlayScene;

// One OpenVR overlay, two QML render pipelines. Only the active pipeline submits its texture
// to the shared handle; the other is parked (handle set invalid) so its QML changes can't
// clobber what's shown. All pointer interaction is delegated to the active scene, so the
// raycaster drives whichever panel is up. This is how the single shared popup shows either the
// Options panel or the Sources list without two OpenVR overlays.
class SwitchablePopupScene : public IOverlayScene {
public:
	enum Panel { Options = 0, Sources = 1 };

	SwitchablePopupScene(const QUrl& optionsQml, const QUrl& sourcesQml);
	~SwitchablePopupScene() override;

	void SetActive(Panel panel);
	// Rebind both panels' `ctl` / `windowManager` context to the overlay the popup now serves.
	void SetContext(OverlayControls* ctl, WindowManager* manager);

	bool Ok() const override;
	bool Ready() const override;
	vr::VROverlayHandle_t Handle() const override { return m_handle; }
	void SetHandle(vr::VROverlayHandle_t handle) override;
	QSize Size() const override;
	bool FireMouseEvent(int hand, Qt::MouseButton button, const glm::vec2& pos) override;
	void MouseNotPresent(int hand) override;
	void FireScroll(int hand, const glm::vec2& pos, int notches) override;
	bool ShowsVrCursor() const override { return true; }

private:
	QmlOverlayScene* active() const { return m_scenes[m_active]; }
	void routeHandles();

	QmlOverlayScene* m_scenes[2] = { nullptr, nullptr }; // [Options, Sources]
	int m_active = 0;
	vr::VROverlayHandle_t m_handle = vr::k_ulOverlayHandleInvalid;
};

// Process-wide shared popup. Teleports in front of whichever control bar opened it and shows
// that overlay's Options or Sources panel, bound to its OverlayControls. Singleton, mirroring
// PointerCursor / GrabIndicator.
class PopupOverlay {
public:
	static PopupOverlay& Instance();

	void Show(OverlayControls* ctl, WindowManager* manager, SwitchablePopupScene::Panel panel,
	          const glm::mat4& transform, float widthMeters);
	void SetTransform(const glm::mat4& transform); // keep it glued to the owning overlay
	void Hide();
	bool Visible() const { return m_visible; }

private:
	PopupOverlay() = default;
	void Ensure();

	Overlay* m_overlay = nullptr;            // owns m_scene
	SwitchablePopupScene* m_scene = nullptr; // observing pointer (owned by m_overlay)
	bool m_visible = false;
};

}
