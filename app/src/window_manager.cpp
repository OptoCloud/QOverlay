#include "window_manager.h"

#include "capture/backend_factory.h"
#include "log.h"
#include "overlay_controls.h"
#include "vr/conversion.h"
#include "vr/overlay.h"
#include "vr/capture_overlay_scene.h"
#include "vr/popup_overlay.h"
#include "vr/transform.h"

#include <QBuffer>
#include <QByteArray>
#include <QCoreApplication>
#include <QDir>
#include <QImage>
#include <QTimer>
#include <QUrl>
#include <QVariantMap>

#include <algorithm>

#include <fmt/core.h>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <glm/geometric.hpp>
#include <openvr.h>

namespace {

QString KindString(QOverlay::Capture::CaptureSurface::Kind kind) {
	return kind == QOverlay::Capture::CaptureSurface::Kind::Monitor
		? QStringLiteral("monitor") : QStringLiteral("window");
}

// Encode a thumbnail QImage as a base64 PNG data URI, directly usable as a QML Image source.
QString ToDataUri(const QImage& image) {
	if (image.isNull()) return {};
	QByteArray png;
	QBuffer buffer(&png);
	buffer.open(QIODevice::WriteOnly);
	if (!image.save(&buffer, "PNG")) return {};
	return QStringLiteral("data:image/png;base64,") + QString::fromLatin1(png.toBase64());
}

}

QOverlay::WindowManager::WindowManager(QObject* parent)
	: QObject(parent)
	, m_backend(Capture::createCaptureBackend(this))
{
	if (m_backend == nullptr) {
		fmt::print("WindowManager: no capture backend available on this platform\n");
	} else {
		fmt::print("WindowManager: capture backend = {}\n", m_backend->name().toStdString());
	}
	refresh();
	refreshKeyboardLegends();

	// Auto-refresh the on-screen keyboard's legends when Windows changes the active layout
	// (Win+Space / Alt+Shift language switch, or focusing a window with a different layout).
	// Poll a cheap layout token and only re-query the heavier legends when it actually changed.
	m_lastLayoutToken = Capture::currentKeyboardLayoutToken();
	auto* layoutTimer = new QTimer(this);
	connect(layoutTimer, &QTimer::timeout, this, [this]() { Guarded("pollKeyboardLayout", [&]() {
		const quint64 token = Capture::currentKeyboardLayoutToken();
		if (token != m_lastLayoutToken && token != 0) {
			m_lastLayoutToken = token;
			refreshKeyboardLegends();
		}
	}); });
	layoutTimer->start(700);
}

void QOverlay::WindowManager::refreshKeyboardLegends() { Guarded("refreshKeyboardLegends", [&]() {
	m_keyboardLegends = Capture::currentKeyboardLegends();
	emit keyboardLegendsChanged();
}); }

QOverlay::WindowManager::~WindowManager() {
	for (auto& active : m_active) destroyActive(active);
	m_active.clear();
}

QVariantList QOverlay::WindowManager::availableSurfaces() const {
	QVariantList list;
	for (const auto& item : m_available) {
		QVariantMap map;
		map["id"] = item.surface.id;
		map["title"] = item.surface.title;
		// Primary label: the app name, falling back to the title when unknown (e.g. monitors).
		map["app"] = item.surface.appName.isEmpty() ? item.surface.title : item.surface.appName;
		map["kind"] = KindString(item.surface.kind);
		map["preview"] = item.preview;
		map["icon"] = item.icon;
		list.append(map);
	}
	return list;
}

QVariantList QOverlay::WindowManager::activeOverlays() const {
	QVariantList list;
	for (const auto& active : m_active) {
		QVariantMap map;
		map["id"] = active.id;
		map["title"] = active.title;
		map["kind"] = KindString(active.kind);
		list.append(map);
	}
	return list;
}

void QOverlay::WindowManager::refresh() { Guarded("refresh", [&]() {
	m_available.clear();
	if (m_backend != nullptr) {
		for (const auto& surface : m_backend->enumerateSurfaces()) {
			m_available.push_back(AvailSurface{
				surface,
				ToDataUri(m_backend->grabThumbnail(surface.id, 360)),
				ToDataUri(m_backend->grabIcon(surface.id, 64)),
			});
		}
	}
	emit availableSurfacesChanged();
}); }

void QOverlay::WindowManager::addSurface(const QString& id) {
	// Defer: this is called from a QML click handler, and spawnSurface creates a control-bar
	// QQuickWindow + GL context + QML component. Doing that synchronously inside QML event
	// delivery reenters Qt's QML/rendering machinery and crashes. Run it after the event
	// unwinds.
	QTimer::singleShot(0, this, [this, id]() { spawnSurface(id); });
}

void QOverlay::WindowManager::spawnSurface(const QString& id) { Guarded("spawnSurface", [&]() {
	if (m_backend == nullptr) return;

	const auto it = std::find_if(m_available.begin(), m_available.end(),
		[&](const AvailSurface& s) { return s.surface.id == id; });
	if (it == m_available.end()) {
		fmt::print("WindowManager: addSurface: unknown surface id {}\n", id.toStdString());
		return;
	}
	const Capture::CaptureSurface surface = it->surface;

	Capture::ICaptureSource* source = m_backend->createSource(id, nullptr);
	if (source == nullptr) {
		fmt::print("WindowManager: failed to create capture source for '{}'\n", surface.title.toStdString());
		return;
	}

	const QString activeId = QString("overlay:%1").arg(m_nextId++);
	const QString key = QString("qoverlay.capture.%1").arg(activeId);

	auto* scene = new VR::CaptureOverlayScene(source); // takes ownership of source
	auto* overlay = new VR::Overlay(key, surface.title, scene, this);
	if (!overlay->Ok()) {
		fmt::print("WindowManager: failed to create overlay for '{}'\n", surface.title.toStdString());
		delete overlay; // deletes the scene, which deletes the source
		return;
	}

	const float width = (surface.kind == Capture::CaptureSurface::Kind::Monitor) ? 1.6f : 1.0f;
	overlay->SetWidth(width);
	placeOverlay(overlay, static_cast<int>(m_active.size()));
	overlay->SetVisible(true);

	// Focus-follow: clicking this overlay makes it the keyboard target.
	connect(source, &Capture::ICaptureSource::interacted, this, [this, source]() {
		m_focused = source;
	});

	// Control bar overlay under this one: opacity / change source / delete.
	auto* controls = new OverlayControls(this, activeId, surface.title, KindString(surface.kind));
	auto* bar = new VR::Overlay(QString("qoverlay.bar.%1").arg(activeId), surface.title + " controls", this);
	if (bar->Ok()) {
		controls->setParent(bar);        // lifetime tied to the bar
		bar->SetGrabbable(false);        // it follows the overlay; not independently grabbable
		// Render the bar (and its upward drop-ups) above the capture overlays it overlaps, but
		// still below the grab chip (200) and pointer cursor (300).
		bar->SetSortOrder(100);
		bar->SetContextProperty("ctl", controls);
		bar->SetContextProperty("windowManager", this); // for the Sources drop-up's live surface list
		bar->SetSource(QUrl::fromLocalFile(QDir(QCoreApplication::applicationDirPath()).filePath("qml/ControlBar.qml")));
		bar->SetWidth(0.5f);
		bar->SetVisible(true);
	} else {
		delete bar; bar = nullptr;
		delete controls; controls = nullptr;
	}

	m_active.push_back(Active{ activeId, id, surface.title, surface.kind, overlay, scene, source, bar, controls, 1.0f });
	positionBar(m_active.back());
	fmt::print("WindowManager: spawned {} overlay for '{}'\n", KindString(surface.kind).toStdString(), surface.title.toStdString());
	emit activeOverlaysChanged();
}); }

void QOverlay::WindowManager::destroyActive(Active& active) {
	if (m_focused == active.source) m_focused = nullptr;
	// If the shared popup is serving this overlay, hide it before the overlay goes away.
	if (m_popupOwnerId == active.id) {
		m_popupOwnerId.clear();
		m_popupPanel.clear();
		VR::PopupOverlay::Instance().Hide();
	}
	// Deferred delete: closeOverlay/closeAll can be triggered from a control-bar click while
	// we're mid-frame inside the raycaster's FireMouseEvent. Deleting synchronously frees an
	// overlay the raycaster still references this frame (use-after-free). deleteLater tears
	// it down in the event loop after the frame, where ~Overlay also clears the raycaster's
	// pointers safely.
	if (active.bar != nullptr) active.bar->deleteLater();          // deletes its OverlayControls child
	if (active.overlay != nullptr) active.overlay->deleteLater();  // destroys VR overlay + scene + source
	active.bar = nullptr;
	active.overlay = nullptr;
	active.source = nullptr;
}

void QOverlay::WindowManager::closeOverlay(const QString& id) { Guarded("closeOverlay", [&]() {
	const auto it = std::find_if(m_active.begin(), m_active.end(),
		[&](const Active& a) { return a.id == id; });
	if (it == m_active.end()) return;

	destroyActive(*it);
	m_active.erase(it);
	fmt::print("WindowManager: closed overlay {}\n", id.toStdString());
	emit activeOverlaysChanged();
}); }

void QOverlay::WindowManager::closeAll() { Guarded("closeAll", [&]() {
	if (m_active.empty()) return;
	for (auto& active : m_active) destroyActive(active);
	m_active.clear();
	fmt::print("WindowManager: closed all overlays\n");
	emit activeOverlaysChanged();
}); }

void QOverlay::WindowManager::setOverlayOpacity(const QString& id, float alpha) { Guarded("setOverlayOpacity", [&]() {
	const auto it = std::find_if(m_active.begin(), m_active.end(),
		[&](const Active& a) { return a.id == id; });
	if (it == m_active.end() || it->overlay == nullptr) return;
	it->opacity = std::clamp(alpha, 0.0f, 1.0f);
	it->overlay->SetOpacity(it->opacity);
}); }

void QOverlay::WindowManager::togglePopup(const QString& id, const QString& panel) {
	// Called from a QML (bar) click. Creating/showing the popup builds a QQuickWindow + GL
	// context on first use, which crashes if done inside QML event delivery — defer like
	// addSurface. Toggling closed is just a Hide, but defer uniformly for simplicity.
	QTimer::singleShot(0, this, [this, id, panel]() { Guarded("togglePopup", [&]() {
	// Same overlay + same panel already open → toggle it closed.
	if (m_popupOwnerId == id && m_popupPanel == panel) { hidePopup(); return; }

	const auto it = std::find_if(m_active.begin(), m_active.end(),
		[&](const Active& a) { return a.id == id; });
	if (it == m_active.end() || it->controls == nullptr) return;

	// Clear the highlight on whoever had it before (if it was a different overlay).
	if (!m_popupOwnerId.isEmpty() && m_popupOwnerId != id) {
		const auto prev = std::find_if(m_active.begin(), m_active.end(),
			[&](const Active& a) { return a.id == m_popupOwnerId; });
		if (prev != m_active.end() && prev->controls != nullptr) prev->controls->setOpenPanel(QString());
	}

	const auto which = (panel == QStringLiteral("sources"))
		? VR::SwitchablePopupScene::Sources : VR::SwitchablePopupScene::Options;

	m_popupOwnerId = id;
	m_popupPanel = panel;
	it->controls->setOpenPanel(panel);

	// Show it, then position it in front of the overlay (positionPopup reads m_popupOwnerId).
	VR::PopupOverlay::Instance().Show(it->controls, this, which, glm::mat4(1.0f), 0.55f);
	positionPopup(*it);
	}); });
}

void QOverlay::WindowManager::hidePopup() { Guarded("hidePopup", [&]() {
	if (m_popupOwnerId.isEmpty()) { VR::PopupOverlay::Instance().Hide(); return; }
	const auto it = std::find_if(m_active.begin(), m_active.end(),
		[&](const Active& a) { return a.id == m_popupOwnerId; });
	if (it != m_active.end() && it->controls != nullptr) it->controls->setOpenPanel(QString());
	m_popupOwnerId.clear();
	m_popupPanel.clear();
	VR::PopupOverlay::Instance().Hide();
}); }

void QOverlay::WindowManager::setOverlayPitchLevel(const QString& id, bool level) { Guarded("setOverlayPitchLevel", [&]() {
	const auto it = std::find_if(m_active.begin(), m_active.end(),
		[&](const Active& a) { return a.id == id; });
	if (it == m_active.end()) return;
	it->pitchLevel = level;
	if (level && it->overlay != nullptr && !it->overlay->IsGrabbed()) it->overlay->LevelPitch();
}); }

void QOverlay::WindowManager::setOverlayLock(const QString& id, const QString& target) { Guarded("setOverlayLock", [&]() {
	const auto it = std::find_if(m_active.begin(), m_active.end(),
		[&](const Active& a) { return a.id == id; });
	if (it == m_active.end() || it->overlay == nullptr) return;
	it->lockTarget = target;

	using DeviceType = VR::TrackedDevice::DeviceType;
	if (target == QStringLiteral("left"))       it->overlay->LockToDevice(DeviceType::ControllerLeft);
	else if (target == QStringLiteral("right")) it->overlay->LockToDevice(DeviceType::ControllerRight);
	else if (target == QStringLiteral("head"))  it->overlay->LockToDevice(DeviceType::HMD);
	else                                        it->overlay->Unlock();
}); }

void QOverlay::WindowManager::retargetOverlay(const QString& id) {
	// Also called from a QML (control-bar) click; defer to stay out of QML event delivery.
	QTimer::singleShot(0, this, [this, id]() { retargetNow(id); });
}

void QOverlay::WindowManager::retargetNow(const QString& id) { Guarded("retargetNow", [&]() {
	const auto it = std::find_if(m_active.begin(), m_active.end(),
		[&](const Active& a) { return a.id == id; });
	if (it == m_active.end() || m_backend == nullptr || m_available.empty()) return;

	// Cycle to the next available surface after the current one.
	int cur = -1;
	for (int i = 0; i < static_cast<int>(m_available.size()); ++i) {
		if (m_available[i].surface.id == it->surfaceId) { cur = i; break; }
	}
	applyRetarget(*it, (cur + 1) % static_cast<int>(m_available.size()));
}); }

void QOverlay::WindowManager::retargetOverlayTo(const QString& id, const QString& surfaceId) {
	// Called from a control-bar click; defer to stay out of QML event delivery (see addSurface).
	QTimer::singleShot(0, this, [this, id, surfaceId]() { Guarded("retargetOverlayTo", [&]() {
		const auto it = std::find_if(m_active.begin(), m_active.end(),
			[&](const Active& a) { return a.id == id; });
		if (it == m_active.end() || m_backend == nullptr) return;

		int idx = -1;
		for (int i = 0; i < static_cast<int>(m_available.size()); ++i) {
			if (m_available[i].surface.id == surfaceId) { idx = i; break; }
		}
		if (idx < 0) {
			fmt::print("WindowManager: retargetOverlayTo: unknown surface id {}\n", surfaceId.toStdString());
			return;
		}
		applyRetarget(*it, idx);
	}); });
}

void QOverlay::WindowManager::applyRetarget(Active& active, int availableIndex) {
	if (availableIndex < 0 || availableIndex >= static_cast<int>(m_available.size())) return;
	const auto& target = m_available[availableIndex].surface;

	Capture::ICaptureSource* newSource = m_backend->createSource(target.id, nullptr);
	if (newSource == nullptr) {
		fmt::print("WindowManager: retarget failed to create source for '{}'\n", target.title.toStdString());
		return;
	}

	if (m_focused == active.source) m_focused = newSource;
	active.scene->SetSource(newSource); // deletes the old source (auto-disconnects its signal)
	connect(newSource, &Capture::ICaptureSource::interacted, this, [this, newSource]() {
		m_focused = newSource;
	});

	active.source = newSource;
	active.surfaceId = target.id;
	active.title = target.title;
	active.kind = target.kind;
	if (active.controls != nullptr) active.controls->setTitle(target.title);
	fmt::print("WindowManager: retargeted overlay {} -> '{}'\n", active.id.toStdString(), target.title.toStdString());
	emit activeOverlaysChanged();
}

void QOverlay::WindowManager::sendText(const QString& text) { Guarded("sendText", [&]() {
	if (m_focused != nullptr) m_focused->injectText(text);
}); }

void QOverlay::WindowManager::sendKey(int qtKey, bool down) { Guarded("sendKey", [&]() {
	if (m_focused != nullptr) m_focused->injectKey(static_cast<Qt::Key>(qtKey), down);
}); }

void QOverlay::WindowManager::sendSystemKey(int qtKey) { Guarded("sendSystemKey", [&]() {
	Capture::injectSystemKey(static_cast<Qt::Key>(qtKey));
}); }

void QOverlay::WindowManager::submitFrames() { Guarded("submitFrames", [&]() {
	for (auto& active : m_active) {
		if (active.scene != nullptr) {
			active.scene->Submit();
		}
		// Keep the overlay upright while "pitch level" is on — but not while it's being grabbed
		// (the hand owns the pose then) or locked to a device (the device owns it).
		if (active.pitchLevel && active.overlay != nullptr
		    && !active.overlay->IsGrabbed() && !active.overlay->IsLocked()) {
			active.overlay->LevelPitch();
		}
		positionBar(active); // keep the control bar under the overlay as it moves/resizes
		if (active.id == m_popupOwnerId) positionPopup(active); // keep the popup glued to it too
	}
}); }

void QOverlay::WindowManager::positionBar(const Active& active) {
	if (active.bar == nullptr || active.overlay == nullptr) return;

	VR::Transform t;
	if (!active.overlay->GetTransformAbsolute(t)) return;

	const glm::mat4 m = t.ToGlmMatrix();
	const float w = active.overlay->Width();
	const QSize sz = active.scene ? active.scene->Size() : QSize();
	const float aspect = (sz.width() > 0) ? static_cast<float>(sz.height()) / static_cast<float>(sz.width()) : 0.5f;
	const float h = w * aspect;

	// The bar is now just the three buttons (the panels moved to the shared popup), so it's a
	// short strip that sits just below the window, a few mm proud along the window's +Z so it
	// doesn't z-fight the window plane.
	const glm::vec3 up = glm::normalize(glm::vec3(m[1]));
	const glm::vec3 forward = glm::normalize(glm::vec3(m[2]));
	const float barWidth = std::clamp(w * 0.45f, 0.18f, 0.5f);
	const float barH = barWidth * 0.16f; // ControlBar aspect (~620x100)
	const glm::vec3 pos = glm::vec3(m[3]) - up * (h * 0.5f + barH * 0.5f + 0.04f) + forward * 0.004f;

	glm::mat4 bm = m;
	bm[3] = glm::vec4(pos, 1.0f);

	active.bar->SetWidth(barWidth);
	active.bar->SetTransformAbsolute(VR::Transform(bm));
}

void QOverlay::WindowManager::positionPopup(const Active& active) {
	if (active.overlay == nullptr || !VR::PopupOverlay::Instance().Visible()) return;

	VR::Transform t;
	if (!active.overlay->GetTransformAbsolute(t)) return;

	// Centre the popup on the window and float it 3cm toward the viewer, so it reads as a
	// distinct layer floating in front of the window rather than pasted onto its surface.
	glm::mat4 m = t.ToGlmMatrix();
	const glm::vec3 forward = glm::normalize(glm::vec3(m[2]));
	m[3] += glm::vec4(forward * 0.03f, 0.0f);
	VR::PopupOverlay::Instance().SetTransform(m);
}

void QOverlay::WindowManager::placeOverlay(VR::Overlay* overlay, int slot) {
	// Spawn in front of wherever the user is currently looking: read the HMD pose, walk out
	// along its (horizontal) forward, fan multiple overlays sideways, and face them back at
	// the user. They can then be grabbed and repositioned. Falls back to a fixed spot ahead
	// of the standing origin if the HMD pose isn't available yet.
	const glm::vec3 worldUp(0.0f, 1.0f, 0.0f);
	constexpr float distance = 1.6f; // metres out from the head
	constexpr float spread = 0.6f;   // sideways gap between fanned overlays

	glm::vec3 eye(0.0f, 1.5f, 0.0f);
	glm::vec3 fwd(0.0f, 0.0f, -1.0f);

	if (auto system = vr::VRSystem()) {
		std::array<vr::TrackedDevicePose_t, vr::k_unMaxTrackedDeviceCount> poses;
		system->GetDeviceToAbsoluteTrackingPose(vr::TrackingUniverseStanding, 0.0f, poses.data(), vr::k_unMaxTrackedDeviceCount);
		const auto& hmd = poses[vr::k_unTrackedDeviceIndex_Hmd];
		if (hmd.bPoseIsValid) {
			const glm::mat4 hmdMat = VR::Conversion::ToGlmMat(hmd.mDeviceToAbsoluteTracking);
			eye = glm::vec3(hmdMat[3]);
			glm::vec3 f = -glm::vec3(hmdMat[2]);
			f.y = 0.0f; // keep overlays level regardless of head pitch
			if (glm::length(f) > 1e-4f) fwd = glm::normalize(f);
		}
	}

	const glm::vec3 right = glm::normalize(glm::cross(fwd, worldUp)); // user's right
	// Fan out symmetrically around straight-ahead: the first overlay lands dead centre, the
	// next ones alternate right/left so a single spawn is right in front of you.
	static constexpr float kFanSteps[] = { 0.0f, 1.0f, -1.0f, 2.0f, -2.0f };
	const float offset = spread * kFanSteps[slot % 5];
	const glm::vec3 pos = eye + fwd * distance + right * offset;

	// An OpenVR overlay shows its texture on its +Z face, so the overlay's +Z must point back
	// toward the eye for the user to see it (horizontal only, so it stays upright).
	glm::vec3 toEye = eye - pos;
	toEye.y = 0.0f;
	const glm::vec3 z = (glm::length(toEye) > 1e-4f) ? glm::normalize(toEye) : -fwd; // local +Z faces the user
	const glm::vec3 x = glm::normalize(glm::cross(worldUp, z));
	const glm::vec3 y = glm::cross(z, x);

	glm::mat4 m(1.0f);
	m[0] = glm::vec4(x, 0.0f);
	m[1] = glm::vec4(y, 0.0f);
	m[2] = glm::vec4(z, 0.0f);
	m[3] = glm::vec4(pos, 1.0f);
	overlay->SetTransformAbsolute(VR::Transform(m));
}
