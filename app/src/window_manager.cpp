#include "window_manager.h"

#include "capture/backend_factory.h"
#include "log.h"
#include "overlay_controls.h"
#include "vr/overlay.h"
#include "vr/capture_overlay_scene.h"
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
		map["kind"] = KindString(item.surface.kind);
		map["preview"] = item.preview;
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
			m_available.push_back(AvailSurface{ surface, ToDataUri(m_backend->grabThumbnail(surface.id, 360)) });
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
		bar->SetContextProperty("ctl", controls);
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
	const auto& target = m_available[(cur + 1) % m_available.size()].surface;

	Capture::ICaptureSource* newSource = m_backend->createSource(target.id, nullptr);
	if (newSource == nullptr) {
		fmt::print("WindowManager: retarget failed to create source for '{}'\n", target.title.toStdString());
		return;
	}

	if (m_focused == it->source) m_focused = newSource;
	it->scene->SetSource(newSource); // deletes the old source (auto-disconnects its signal)
	connect(newSource, &Capture::ICaptureSource::interacted, this, [this, newSource]() {
		m_focused = newSource;
	});

	it->source = newSource;
	it->surfaceId = target.id;
	it->title = target.title;
	it->kind = target.kind;
	if (it->controls != nullptr) it->controls->setTitle(target.title);
	fmt::print("WindowManager: retargeted overlay {} -> '{}'\n", id.toStdString(), target.title.toStdString());
	emit activeOverlaysChanged();
}); }

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
		positionBar(active); // keep the control bar under the overlay as it moves/resizes
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

	// Sit just below the overlay (down its local -Y), same orientation.
	const glm::vec3 up = glm::normalize(glm::vec3(m[1]));
	const float barWidth = std::clamp(w * 0.55f, 0.2f, 0.6f);
	const glm::vec3 pos = glm::vec3(m[3]) - up * (h * 0.5f + 0.06f);

	glm::mat4 bm = m;
	bm[3] = glm::vec4(pos, 1.0f);

	active.bar->SetWidth(barWidth);
	active.bar->SetTransformAbsolute(VR::Transform(bm));
}

void QOverlay::WindowManager::placeOverlay(VR::Overlay* overlay, int slot) {
	// Fan new overlays out horizontally in a shallow arc in front of the user; they can
	// then be grabbed and repositioned with the controllers.
	const float x = -0.9f + 0.6f * static_cast<float>(slot % 4);
	const float y = 1.5f;
	const float z = -2.0f;

	vr::HmdMatrix34_t transform = {};
	transform.m[0][0] = 1.0f;
	transform.m[1][1] = 1.0f;
	transform.m[2][2] = 1.0f;
	transform.m[0][3] = x;
	transform.m[1][3] = y;
	transform.m[2][3] = z;

	if (auto* vroverlay = vr::VROverlay()) {
		vroverlay->SetOverlayTransformAbsolute(overlay->Handle(), vr::TrackingUniverseStanding, &transform);
	}
}
