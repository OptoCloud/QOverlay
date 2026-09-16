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

void QOverlay::OverlayControls::setPitchLevel(bool level) {
	if (m_pitchLevel == level) return;
	m_pitchLevel = level;
	if (m_manager != nullptr) m_manager->setOverlayPitchLevel(m_id, level);
	emit pitchLevelChanged();
}

void QOverlay::OverlayControls::setLockTarget(const QString& target) {
	if (m_lockTarget == target) return;
	m_lockTarget = target;
	if (m_manager != nullptr) m_manager->setOverlayLock(m_id, target);
	emit lockTargetChanged();
}

void QOverlay::OverlayControls::setTitle(const QString& title) {
	if (m_title == title) return;
	m_title = title;
	emit titleChanged();
}

void QOverlay::OverlayControls::deleteOverlay() {
	if (m_manager != nullptr) m_manager->closeOverlay(m_id);
}

void QOverlay::OverlayControls::setSource(const QString& surfaceId) {
	if (m_manager != nullptr) m_manager->retargetOverlayTo(m_id, surfaceId);
}

void QOverlay::OverlayControls::setOpenPanel(const QString& panel) {
	if (m_openPanel == panel) return;
	m_openPanel = panel;
	emit openPanelChanged();
}

void QOverlay::OverlayControls::openOptions() {
	if (m_manager != nullptr) m_manager->togglePopup(m_id, QStringLiteral("options"));
}

void QOverlay::OverlayControls::openSources() {
	if (m_manager != nullptr) m_manager->togglePopup(m_id, QStringLiteral("sources"));
}

void QOverlay::OverlayControls::closePanel() {
	if (m_manager != nullptr) m_manager->hidePopup();
}
