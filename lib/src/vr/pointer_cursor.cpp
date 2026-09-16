#include "vr/pointer_cursor.h"

#include <QImage>
#include <QPainter>
#include <QRadialGradient>

#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

namespace {

// Render the cursor dot: a soft-edged filled circle with a subtle outline. RGBA8888 so the
// bytes match IVROverlay::SetOverlayRaw. Idle = blue, pressed = amber (matches the old QML
// dot colors #4f9dff / #ffd166).
QImage MakeDot(bool pressed) {
	constexpr int S = 48;
	QImage image(S, S, QImage::Format_RGBA8888);
	image.fill(Qt::transparent);

	const QColor fill = pressed ? QColor(255, 209, 102) : QColor(79, 157, 255);
	const QColor ring = pressed ? QColor(120, 90, 20, 230) : QColor(20, 50, 110, 230);

	QPainter p(&image);
	p.setRenderHint(QPainter::Antialiasing);

	const double c = S / 2.0;
	const double r = S / 2.0 - 6.0;

	// Outline ring for contrast against bright/dark content.
	p.setPen(Qt::NoPen);
	p.setBrush(ring);
	p.drawEllipse(QPointF(c, c), r + 2.5, r + 2.5);

	// Filled dot with a faint highlight toward the top-left.
	QRadialGradient grad(QPointF(c - r * 0.3, c - r * 0.3), r * 1.6);
	grad.setColorAt(0.0, fill.lighter(130));
	grad.setColorAt(1.0, fill);
	p.setBrush(grad);
	p.drawEllipse(QPointF(c, c), r, r);

	p.end();
	return image;
}

}

QOverlay::VR::PointerCursor& QOverlay::VR::PointerCursor::Instance() {
	static PointerCursor instance;
	return instance;
}

void QOverlay::VR::PointerCursor::Ensure(int hand) {
	Cursor& cur = m_cursors[hand];
	if (cur.handle != vr::k_ulOverlayHandleInvalid) return;
	auto overlay = vr::VROverlay();
	if (overlay == nullptr) return;

	const char* key = (hand == 0) ? "qoverlay.cursor.left" : "qoverlay.cursor.right";
	const char* name = (hand == 0) ? "QOverlay Cursor L" : "QOverlay Cursor R";
	if (overlay->CreateOverlay(key, name, &cur.handle) != vr::VROverlayError_None) {
		cur.handle = vr::k_ulOverlayHandleInvalid;
		return;
	}
	overlay->SetOverlayInputMethod(cur.handle, vr::VROverlayInputMethod_None);
	overlay->SetOverlayWidthInMeters(cur.handle, 0.012f);
	overlay->SetOverlaySortOrder(cur.handle, 300); // above the panel it rides and the grab chip (200)
}

void QOverlay::VR::PointerCursor::Show(int hand, const glm::mat4& overlayTransform, const glm::vec3& hitWorld, bool pressed) {
	if (hand < 0 || hand > 1) return;
	Ensure(hand);
	Cursor& cur = m_cursors[hand];
	if (cur.handle == vr::k_ulOverlayHandleInvalid) return;
	auto overlay = vr::VROverlay();
	if (overlay == nullptr) return;

	const int wantColor = pressed ? 1 : 0;
	if (wantColor != cur.color) {
		static const QImage idleDot = MakeDot(false);
		static const QImage pressedDot = MakeDot(true);
		const QImage& dot = pressed ? pressedDot : idleDot;
		overlay->SetOverlayRaw(cur.handle, const_cast<uchar*>(dot.constBits()), dot.width(), dot.height(), 4);
		cur.color = wantColor;
	}

	// Take the overlay's orientation (so the dot lies flat on the panel and faces the same
	// way), place it at the ray→plane hit, and nudge a touch toward the viewer (the overlay's
	// +Z front, as in GrabIndicator) so it floats just above the surface instead of z-fighting.
	glm::mat4 m = overlayTransform;
	m[3] = glm::vec4(hitWorld, 1.0f);
	m[3] += glm::vec4(glm::vec3(m[2]) * 0.006f, 0.0f);

	vr::HmdMatrix34_t mat = {};
	mat.m[0][0] = m[0][0]; mat.m[1][0] = m[0][1]; mat.m[2][0] = m[0][2];
	mat.m[0][1] = m[1][0]; mat.m[1][1] = m[1][1]; mat.m[2][1] = m[1][2];
	mat.m[0][2] = m[2][0]; mat.m[1][2] = m[2][1]; mat.m[2][2] = m[2][2];
	mat.m[0][3] = m[3][0]; mat.m[1][3] = m[3][1]; mat.m[2][3] = m[3][2];
	overlay->SetOverlayTransformAbsolute(cur.handle, vr::TrackingUniverseStanding, &mat);

	if (!cur.visible) {
		overlay->ShowOverlay(cur.handle);
		cur.visible = true;
	}
}

void QOverlay::VR::PointerCursor::Hide(int hand) {
	if (hand < 0 || hand > 1) return;
	Cursor& cur = m_cursors[hand];
	if (!cur.visible) return;
	if (auto overlay = vr::VROverlay(); overlay != nullptr && cur.handle != vr::k_ulOverlayHandleInvalid) {
		overlay->HideOverlay(cur.handle);
	}
	cur.visible = false;
}
