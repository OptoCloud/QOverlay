#include "vr/popup_overlay.h"

#include "log.h"
#include "overlay_controls.h"
#include "vr/overlay.h"
#include "vr/qml_overlay_scene.h"
#include "vr/transform.h"
#include "window_manager.h"

#include <QCoreApplication>
#include <QDir>

using QOverlay::VR::PopupOverlay;
using QOverlay::VR::SwitchablePopupScene;

SwitchablePopupScene::SwitchablePopupScene(const QUrl& optionsQml, const QUrl& sourcesQml) {
	m_scenes[0] = new QmlOverlayScene();
	m_scenes[1] = new QmlOverlayScene();
	m_scenes[0]->SetSource(optionsQml);
	m_scenes[1]->SetSource(sourcesQml);
}

SwitchablePopupScene::~SwitchablePopupScene() {
	delete m_scenes[0];
	delete m_scenes[1];
}

void SwitchablePopupScene::routeHandles() {
	// Only the active scene owns the shared handle (so only it submits its texture); the other
	// is parked with an invalid handle so its QML re-renders are dropped in renderVR().
	if (m_scenes[m_active] != nullptr) m_scenes[m_active]->SetHandle(m_handle);
	if (m_scenes[1 - m_active] != nullptr) m_scenes[1 - m_active]->SetHandle(vr::k_ulOverlayHandleInvalid);
}

void SwitchablePopupScene::SetActive(Panel panel) {
	const int i = (panel == Sources) ? 1 : 0;
	if (i == m_active && m_scenes[i] != nullptr && m_scenes[i]->Handle() == m_handle) return;
	m_active = i;
	routeHandles();
}

void SwitchablePopupScene::SetContext(OverlayControls* ctl, WindowManager* manager) {
	for (auto* scene : m_scenes) {
		if (scene == nullptr) continue;
		scene->SetContextProperty("ctl", ctl);
		scene->SetContextProperty("windowManager", manager);
	}
}

bool SwitchablePopupScene::Ok() const {
	return m_scenes[0] != nullptr && m_scenes[1] != nullptr && m_scenes[0]->Ok() && m_scenes[1]->Ok();
}

bool SwitchablePopupScene::Ready() const {
	return active() != nullptr && active()->Ready();
}

void SwitchablePopupScene::SetHandle(vr::VROverlayHandle_t handle) {
	m_handle = handle;
	routeHandles();
}

QSize SwitchablePopupScene::Size() const {
	return active() != nullptr ? active()->Size() : QSize();
}

bool SwitchablePopupScene::FireMouseEvent(int hand, Qt::MouseButton button, const glm::vec2& pos) {
	return active() != nullptr ? active()->FireMouseEvent(hand, button, pos) : false;
}

void SwitchablePopupScene::MouseNotPresent(int hand) {
	if (active() != nullptr) active()->MouseNotPresent(hand);
}

void SwitchablePopupScene::FireScroll(int hand, const glm::vec2& pos, int notches) {
	if (active() != nullptr) active()->FireScroll(hand, pos, notches);
}

PopupOverlay& PopupOverlay::Instance() {
	static PopupOverlay instance;
	return instance;
}

void PopupOverlay::Ensure() {
	if (m_overlay != nullptr) return;

	const QString base = QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("qml"));
	const QUrl optionsUrl = QUrl::fromLocalFile(base + QStringLiteral("/OptionsPanel.qml"));
	const QUrl sourcesUrl = QUrl::fromLocalFile(base + QStringLiteral("/SourcesPanel.qml"));

	m_scene = new SwitchablePopupScene(optionsUrl, sourcesUrl);
	m_overlay = new Overlay(QStringLiteral("qoverlay.popup"), QStringLiteral("QOverlay Popup"), m_scene, nullptr);
	if (!m_overlay->Ok()) {
		LOG_ERROR("PopupOverlay: failed to create shared popup overlay");
	}
	m_overlay->SetGrabbable(false);
	// Above the bars (100) it floats over, still below the grab chip (200) and cursor (300).
	m_overlay->SetSortOrder(150);
}

void PopupOverlay::Show(OverlayControls* ctl, WindowManager* manager, SwitchablePopupScene::Panel panel,
                        const glm::mat4& transform, float widthMeters) {
	Ensure();
	if (m_overlay == nullptr || m_scene == nullptr) return;

	m_scene->SetContext(ctl, manager);
	m_scene->SetActive(panel);
	m_overlay->SetWidth(widthMeters);
	m_overlay->SetTransformAbsolute(Transform(transform));
	m_overlay->SetVisible(true);
	m_visible = true;
}

void PopupOverlay::SetTransform(const glm::mat4& transform) {
	if (m_overlay != nullptr && m_visible) m_overlay->SetTransformAbsolute(Transform(transform));
}

void PopupOverlay::Hide() {
	if (m_overlay != nullptr && m_visible) m_overlay->SetVisible(false);
	m_visible = false;
}
