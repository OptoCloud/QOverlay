#pragma once

#include "config.h"

#include <QObject>
#include <QString>

namespace QOverlay {

// Bridges the QML control-panel overlay to the C++ app. Exposed to QML as `app`.
// Kept intentionally small for now — the window-manager surface list (monitors /
// windows to spawn as overlays) will be added here in a later phase.
class OverlayBridge : public QObject {
	Q_OBJECT
	Q_DISABLE_COPY(OverlayBridge)
	Q_PROPERTY(QString status READ status NOTIFY statusChanged)
	Q_PROPERTY(bool leftHanded READ leftHanded NOTIFY leftHandedChanged)
	Q_PROPERTY(bool panelVisible READ panelVisible NOTIFY panelVisibleChanged)
	Q_PROPERTY(bool keyboardVisible READ keyboardVisible NOTIFY keyboardVisibleChanged)
	// Global input-smoothing amounts (0..1), persisted in Config. Live everywhere immediately.
	Q_PROPERTY(qreal cursorSmoothing READ cursorSmoothing WRITE setCursorSmoothing NOTIFY smoothingChanged)
	Q_PROPERTY(qreal dragSmoothing READ dragSmoothing WRITE setDragSmoothing NOTIFY smoothingChanged)
public:
	using QObject::QObject;

	QString status() const { return m_status; }
	bool leftHanded() const { return m_leftHanded; }
	bool panelVisible() const { return m_panelVisible; }
	bool keyboardVisible() const { return m_keyboardVisible; }
	qreal cursorSmoothing() const { return Config::Instance().CursorSmoothing(); }
	qreal dragSmoothing() const { return Config::Instance().DragSmoothing(); }

	void setCursorSmoothing(qreal amount) {
		if (qFuzzyCompare(cursorSmoothing(), amount)) return;
		Config::Instance().SetCursorSmoothing(static_cast<float>(amount));
		emit smoothingChanged();
	}
	void setDragSmoothing(qreal amount) {
		if (qFuzzyCompare(dragSmoothing(), amount)) return;
		Config::Instance().SetDragSmoothing(static_cast<float>(amount));
		emit smoothingChanged();
	}

	Q_INVOKABLE void switchHand() { emit switchHandRequested(); }
	// Toggle the expandable panel (from the wrist launcher).
	Q_INVOKABLE void togglePanel() { emit panelToggleRequested(); }
	// Toggle the on-screen keyboard overlay (from the panel).
	Q_INVOKABLE void toggleKeyboard() { emit keyboardToggleRequested(); }

public slots:
	void setStatus(const QString& status) {
		if (m_status == status) return;
		m_status = status;
		emit statusChanged(m_status);
	}
	void setLeftHanded(bool leftHanded) {
		if (m_leftHanded == leftHanded) return;
		m_leftHanded = leftHanded;
		emit leftHandedChanged(m_leftHanded);
	}
	void setPanelVisible(bool visible) {
		if (m_panelVisible == visible) return;
		m_panelVisible = visible;
		emit panelVisibleChanged(m_panelVisible);
	}
	void setKeyboardVisible(bool visible) {
		if (m_keyboardVisible == visible) return;
		m_keyboardVisible = visible;
		emit keyboardVisibleChanged(m_keyboardVisible);
	}

signals:
	void switchHandRequested();
	void panelToggleRequested();
	void keyboardToggleRequested();
	void statusChanged(const QString& status);
	void leftHandedChanged(bool leftHanded);
	void panelVisibleChanged(bool visible);
	void keyboardVisibleChanged(bool visible);
	void smoothingChanged();

private:
	QString m_status = "Ready";
	bool m_leftHanded = true;
	bool m_panelVisible = false;
	bool m_keyboardVisible = false;
};

}
