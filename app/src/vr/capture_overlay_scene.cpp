#include "vr/capture_overlay_scene.h"

#include <QPointF>

QOverlay::VR::CaptureOverlayScene::CaptureOverlayScene(Capture::ICaptureSource* source)
	: m_source(source)
{
}

// Defined here (not defaulted in the header) so the unique_ptr sees the complete
// ICaptureSource type from this translation unit.
QOverlay::VR::CaptureOverlayScene::~CaptureOverlayScene() = default;

bool QOverlay::VR::CaptureOverlayScene::FireMouseEvent(int /*hand*/, Qt::MouseButton button, const glm::vec2& pos) {
	if (m_source == nullptr) return false;
	m_source->injectMouse(button, QPointF(pos.x, pos.y));
	return true;
}

void QOverlay::VR::CaptureOverlayScene::MouseNotPresent(int /*hand*/) {
	if (m_source != nullptr) m_source->mouseGone();
}

void QOverlay::VR::CaptureOverlayScene::FireScroll(int /*hand*/, const glm::vec2& pos, int notches) {
	if (m_source != nullptr) m_source->injectScroll(notches, QPointF(pos.x, pos.y));
}

void QOverlay::VR::CaptureOverlayScene::Submit() {
	if (m_source != nullptr) m_source->submit(m_handle);
}

void QOverlay::VR::CaptureOverlayScene::SetSource(Capture::ICaptureSource* source) {
	if (m_source.get() == source) return;
	m_source.reset(source); // destroys the previous source (auto-disconnects its signals)
}
