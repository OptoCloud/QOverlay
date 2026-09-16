#include "overlay_controls.h"

#include "window_manager.h"

QOverlay::OverlayControls::OverlayControls(WindowManager* manager, const QString& id, const QString& title, const QString& kind, QObject* parent)
	: QObject(parent)
	, m_manager(manager)
	, m_id(id)
	, m_title(title)
	, m_kind(kind)
{
}

void QOverlay::OverlayControls::setOpacity(qreal opacity) {
	if (qFuzzyCompare(m_opacity, opacity)) return;
	m_opacity = opacity;
	if (m_manager != nullptr) m_manager->setOverlayOpacity(m_id, static_cast<float>(opacity));
	emit opacityChanged();
}

void QOverlay::OverlayControls::setTitle(const QString& title) {
	if (m_title == title) return;
	m_title = title;
	emit titleChanged();
}

void QOverlay::OverlayControls::deleteOverlay() {
	if (m_manager != nullptr) m_manager->closeOverlay(m_id);
}

void QOverlay::OverlayControls::changeSource() {
	if (m_manager != nullptr) m_manager->retargetOverlay(m_id);
}
