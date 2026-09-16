#include "vr/grab_indicator.h"

#include <cmath>

#include <QImage>
#include <QPainter>
#include <QPen>

#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

namespace {

// Render the symbol chip: a rounded translucent panel with white arrows — 4-way (move) or
// diagonal (resize). RGBA8888 so the bytes match IVROverlay::SetOverlayRaw.
QImage MakeGlyph(bool scaling) {
	constexpr int S = 160;
	QImage image(S, S, QImage::Format_RGBA8888);
	image.fill(Qt::transparent);

	QPainter p(&image);
	p.setRenderHint(QPainter::Antialiasing);

	const QRectF chip(10, 10, S - 20, S - 20);
	p.setPen(Qt::NoPen);
	p.setBrush(QColor(16, 20, 28, 205));
	p.drawRoundedRect(chip, 30, 30);
	p.setBrush(Qt::NoBrush);
	p.setPen(QPen(QColor(91, 139, 255, 230), 4));
	p.drawRoundedRect(chip, 30, 30);

	p.setPen(QPen(QColor(255, 255, 255), 11, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
	const double c = S / 2.0;
	auto arrow = [&](double x2, double y2) {
		p.drawLine(QPointF(c, c), QPointF(x2, y2));
		const double ang = std::atan2(y2 - c, x2 - c);
		const double a = 20.0, spread = 0.5;
		p.drawLine(QPointF(x2, y2), QPointF(x2 - a * std::cos(ang - spread), y2 - a * std::sin(ang - spread)));
		p.drawLine(QPointF(x2, y2), QPointF(x2 - a * std::cos(ang + spread), y2 - a * std::sin(ang + spread)));
	};
	if (scaling) {
		arrow(S - 44, 44);   // NE
		arrow(44, S - 44);   // SW
	} else {
		arrow(c, 42);        // up
		arrow(c, S - 42);    // down
		arrow(42, c);        // left
		arrow(S - 42, c);    // right
	}
	p.end();
	return image;
}

}

QOverlay::VR::GrabIndicator& QOverlay::VR::GrabIndicator::Instance() {
	static GrabIndicator instance;
	return instance;
}

void QOverlay::VR::GrabIndicator::Ensure() {
	if (m_handle != vr::k_ulOverlayHandleInvalid) return;
	auto overlay = vr::VROverlay();
	if (overlay == nullptr) return;

	if (overlay->CreateOverlay("qoverlay.grabindicator", "QOverlay Grab", &m_handle) != vr::VROverlayError_None) {
		m_handle = vr::k_ulOverlayHandleInvalid;
		return;
	}
	overlay->SetOverlayInputMethod(m_handle, vr::VROverlayInputMethod_None);
	overlay->SetOverlayWidthInMeters(m_handle, 0.12f);
	overlay->SetOverlaySortOrder(m_handle, 200); // draw on top of the overlay it marks
}

void QOverlay::VR::GrabIndicator::Show(const glm::mat4& overlayTransform, bool scaling) {
	Ensure();
	if (m_handle == vr::k_ulOverlayHandleInvalid) return;
	auto overlay = vr::VROverlay();
	if (overlay == nullptr) return;

	const int wantGlyph = scaling ? 1 : 0;
	if (wantGlyph != m_glyph) {
		static const QImage moveGlyph = MakeGlyph(false);
		static const QImage scaleGlyph = MakeGlyph(true);
		const QImage& glyph = scaling ? scaleGlyph : moveGlyph;
		overlay->SetOverlayRaw(m_handle, const_cast<uchar*>(glyph.constBits()), glyph.width(), glyph.height(), 4);
		m_glyph = wantGlyph;
	}

	// Same pose as the overlay, nudged a touch toward the viewer (the overlay's +Z front).
	glm::mat4 m = overlayTransform;
	m[3] += glm::vec4(glm::vec3(m[2]) * 0.02f, 0.0f);
	vr::HmdMatrix34_t mat = {};
	mat.m[0][0] = m[0][0]; mat.m[1][0] = m[0][1]; mat.m[2][0] = m[0][2];
	mat.m[0][1] = m[1][0]; mat.m[1][1] = m[1][1]; mat.m[2][1] = m[1][2];
	mat.m[0][2] = m[2][0]; mat.m[1][2] = m[2][1]; mat.m[2][2] = m[2][2];
	mat.m[0][3] = m[3][0]; mat.m[1][3] = m[3][1]; mat.m[2][3] = m[3][2];
	overlay->SetOverlayTransformAbsolute(m_handle, vr::TrackingUniverseStanding, &mat);

	if (!m_visible) {
		overlay->ShowOverlay(m_handle);
		m_visible = true;
	}
}

void QOverlay::VR::GrabIndicator::Hide() {
	if (!m_visible) return;
	if (auto overlay = vr::VROverlay(); overlay != nullptr && m_handle != vr::k_ulOverlayHandleInvalid) {
		overlay->HideOverlay(m_handle);
	}
	m_visible = false;
}
