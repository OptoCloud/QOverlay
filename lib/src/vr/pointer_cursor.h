#pragma once

#include <openvr.h>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

namespace QOverlay::VR {

// Per-hand VR mouse cursor, each rendered as its own small OpenVR overlay (one per hand)
// instead of a QML Rectangle inside the panel. Moving the pointer therefore never
// re-rasterizes the QML scene it hovers — OpenVR composites the cursors for free. Driven
// each poll by the raycaster: Show() it at the ray→overlay-plane hit, Hide() when the hand
// isn't over a cursor-drawing (QML) overlay. Mirrors GrabIndicator's single-shared-overlay
// approach, one handle per hand.
class PointerCursor {
public:
	static PointerCursor& Instance();

	// hand: 0 = left, 1 = right. `overlayTransform` is the hovered overlay's absolute
	// transform (supplies the cursor's orientation so the dot lies flat on the panel and
	// faces the viewer); `hitWorld` is the world-space point on the plane where the ray
	// lands; `pressed` swaps the dot to the pressed color.
	void Show(int hand, const glm::mat4& overlayTransform, const glm::vec3& hitWorld, bool pressed);
	void Hide(int hand);

private:
	PointerCursor() = default;
	void Ensure(int hand);

	struct Cursor {
		vr::VROverlayHandle_t handle = vr::k_ulOverlayHandleInvalid;
		bool visible = false;
		int color = -1; // -1 none, 0 idle, 1 pressed — avoids re-uploading the texture each poll
	};
	Cursor m_cursors[2];
};

}
