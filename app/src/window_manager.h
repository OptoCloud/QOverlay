#pragma once

#include "capture/capture_backend.h"

#include <vector>

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

namespace QOverlay::VR {
class Overlay;
class CaptureOverlayScene;
}

namespace QOverlay {

class OverlayControls;

// The window-manager controller, exposed to the control-panel QML as `windowManager`.
// Backend-neutral: it drives an ICaptureBackend (selected at build time) to enumerate
// surfaces, grab previews, and create capture sources; it spawns/destroys the resulting
// overlays at runtime. The VR raycaster picks up spawned overlays automatically, so
// grab-to-move works with no extra wiring.
class WindowManager : public QObject {
	Q_OBJECT
	Q_DISABLE_COPY(WindowManager)

	// Available surfaces to spawn, and the currently-active overlays. Each element is a
	// map { id, title, kind, preview } — preview is a base64 PNG data URI (available only).
	Q_PROPERTY(QVariantList availableSurfaces READ availableSurfaces NOTIFY availableSurfacesChanged)
	Q_PROPERTY(QVariantList activeOverlays READ activeOverlays NOTIFY activeOverlaysChanged)
	// Current OS keyboard-layout legends for the virtual keyboard (see currentKeyboardLegends).
	Q_PROPERTY(QVariantMap keyboardLegends READ keyboardLegends NOTIFY keyboardLegendsChanged)

public:
	explicit WindowManager(QObject* parent = nullptr);
	~WindowManager() override;

	QVariantList availableSurfaces() const;
	QVariantList activeOverlays() const;

	// Re-enumerate monitors/windows (and their previews) into availableSurfaces.
	Q_INVOKABLE void refresh();
	// Spawn a capture overlay for the surface with this id (from availableSurfaces).
	Q_INVOKABLE void addSurface(const QString& id);
	// Destroy the active overlay with this id (from activeOverlays).
	Q_INVOKABLE void closeOverlay(const QString& id);
	// Destroy every active overlay.
	Q_INVOKABLE void closeAll();

	// Per-overlay control-bar actions (called from OverlayControls).
	void setOverlayOpacity(const QString& id, float alpha);
	void retargetOverlay(const QString& id);                                 // cycle to the next available surface
	void retargetOverlayTo(const QString& id, const QString& surfaceId);     // retarget to a chosen surface
	void setOverlayPitchLevel(const QString& id, bool level);                // keep the overlay upright
	void setOverlayLock(const QString& id, const QString& target);           // off / left / right / head

	// Shared popup: toggle it to this overlay's Options/Sources panel (re-teleports + rebinds if
	// it was showing for another overlay), or hide it. Called from the control-bar buttons.
	void togglePopup(const QString& id, const QString& panel);
	void hidePopup();

	// Route virtual-keyboard input to the focused capture overlay (the last one clicked).
	// `qtKey` is a Qt::Key value; text is Unicode printable input.
	Q_INVOKABLE void sendText(const QString& text);
	Q_INVOKABLE void sendKey(int qtKey, bool down);
	// Tap a key globally via the OS (not routed to a captured window). For shell/system keys
	// like the Windows key, which open Start from global input rather than a window message.
	Q_INVOKABLE void sendSystemKey(int qtKey);
	// True when there is a focused target to receive keyboard input.
	Q_INVOKABLE bool hasKeyboardFocus() const { return m_focused != nullptr; }

	QVariantMap keyboardLegends() const { return m_keyboardLegends; }
	// Re-query the OS keyboard layout (call when showing the keyboard / on layout change).
	Q_INVOKABLE void refreshKeyboardLegends();

	// Push the latest captured frame for every active overlay. Call once per VR tick.
	void submitFrames();

signals:
	void availableSurfacesChanged();
	void activeOverlaysChanged();
	void keyboardLegendsChanged();

private:
	struct AvailSurface {
		Capture::CaptureSurface surface;
		QString preview; // base64 PNG data URI, or empty if the grab failed
		QString icon;    // base64 PNG app-icon data URI, or empty (windows only)
	};

	struct Active {
		QString id;
		QString surfaceId; // the available-surface id this was created from (for retarget)
		QString title;
		Capture::CaptureSurface::Kind kind;
		VR::Overlay* overlay = nullptr;
		VR::CaptureOverlayScene* scene = nullptr;    // owned by `overlay`, non-owning here
		Capture::ICaptureSource* source = nullptr;   // owned by `scene`, non-owning here
		VR::Overlay* bar = nullptr;                  // control-bar overlay under this one
		OverlayControls* controls = nullptr;         // owned by `bar`
		float opacity = 1.0f;
		bool pitchLevel = false;                     // keep upright (re-leveled each tick)
		QString lockTarget = QStringLiteral("off");  // off / left / right / head
	};

	// The actual work behind addSurface/retargetOverlay, run deferred (QTimer::singleShot 0)
	// so the reentrant QML/GL scene creation doesn't happen inside a QML event handler
	// (which crashes Qt — exceptions/reentrancy through QML event delivery).
	void spawnSurface(const QString& id);
	void retargetNow(const QString& id);
	// Shared source-swap for an already-resolved active overlay + available surface index.
	void applyRetarget(Active& active, int availableIndex);

	void placeOverlay(VR::Overlay* overlay, int slot);
	void positionBar(const Active& active);          // place the bar under the overlay
	void positionPopup(const Active& active);        // place the shared popup in front of the overlay
	void destroyActive(Active& active);              // tear down overlay + bar

	Capture::ICaptureBackend* m_backend = nullptr;
	std::vector<AvailSurface> m_available;
	std::vector<Active> m_active;
	Capture::ICaptureSource* m_focused = nullptr; // keyboard target (last-clicked overlay)
	QString m_popupOwnerId;                       // active overlay the shared popup serves ("" = hidden)
	QString m_popupPanel;                         // "options" or "sources"
	QVariantMap m_keyboardLegends;
	quint64 m_lastLayoutToken = 0; // last-seen OS layout, for auto-refreshing legends on change
	int m_nextId = 0;
};

}
