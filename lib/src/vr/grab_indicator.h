#pragma once

#include <openvr.h>
#include <glm/mat4x4.hpp>

namespace QOverlay::VR {

// A single shared symbol chip floated over whichever overlay is currently being grabbed or
// resized (4-way move arrows / diagonal resize arrows). Driven each frame by the raycaster:
// Show() it over the grabbed overlay, Hide() when nothing is grabbed. One overlay reused
// for all grabs rather than one per overlay.
class GrabIndicator {
public:
	static GrabIndicator& Instance();

	// Position the chip at `overlayTransform` (the grabbed overlay's absolute transform) and
	// show it with the move (scaling=false) or resize (scaling=true) glyph.
	void Show(const glm::mat4& overlayTransform, bool scaling);
	void Hide();

private:
	GrabIndicator() = default;
	void Ensure();

	vr::VROverlayHandle_t m_handle = vr::k_ulOverlayHandleInvalid;
	bool m_visible = false;
	int m_glyph = -1; // -1 none, 0 move, 1 scale — avoids re-uploading the texture each frame
};

}
