#pragma once

// Lightweight stand-ins for the context objects the VR app injects into Keyboard.qml, so the
// keyboard can be exercised on the desktop with a mouse instead of in a headset.
#include "capture/backend_factory.h"

#include <QObject>
#include <QString>
#include <QVariantMap>
#include <QtCore/qnamespace.h>

// Mirrors QOverlay::VR::PointerState: the QML reads x/y/active/pressed and reacts to
// pressedDown for key activation. Driven here by the mouse instead of a controller ray.
class TesterPointer : public QObject {
	Q_OBJECT
	Q_PROPERTY(qreal x READ x NOTIFY changed)
	Q_PROPERTY(qreal y READ y NOTIFY changed)
	Q_PROPERTY(bool active READ active NOTIFY changed)
	Q_PROPERTY(bool pressed READ pressed NOTIFY changed)
public:
	using QObject::QObject;

	qreal x() const { return m_x; }
	qreal y() const { return m_y; }
	bool active() const { return m_active; }
	bool pressed() const { return m_pressed; }

	void set(qreal x, qreal y, bool active, bool pressed) {
		if (m_x == x && m_y == y && m_active == active && m_pressed == pressed) return;
		const bool rising = pressed && !m_pressed;
		m_x = x; m_y = y; m_active = active; m_pressed = pressed;
		emit changed();
		if (rising) emit pressedDown();
	}

signals:
	void changed();
	void pressedDown();

private:
	qreal m_x = 0, m_y = 0;
	bool m_active = false, m_pressed = false;
};

// Stands in for WindowManager: serves the real OS keyboard legends and echoes injected input
// into a `typed` string so you can see what the keyboard would send.
class TesterWindowManager : public QObject {
	Q_OBJECT
	Q_PROPERTY(QVariantMap keyboardLegends READ keyboardLegends NOTIFY keyboardLegendsChanged)
	Q_PROPERTY(QString typed READ typed NOTIFY typedChanged)
public:
	using QObject::QObject;

	QVariantMap keyboardLegends() const { return m_legends; }
	QString typed() const { return m_typed; }

	Q_INVOKABLE bool hasKeyboardFocus() const { return true; }
	Q_INVOKABLE void refreshKeyboardLegends() {
		m_legends = QOverlay::Capture::currentKeyboardLegends();
		emit keyboardLegendsChanged();
	}
	Q_INVOKABLE void sendText(const QString& text) { m_typed += text; emit typedChanged(); }
	Q_INVOKABLE void sendKey(int qtKey, bool down) {
		if (!down) return; // echo on press only
		switch (qtKey) {
			case Qt::Key_Backspace: m_typed.chop(1); emit typedChanged(); break;
			case Qt::Key_Return:
			case Qt::Key_Enter:     m_typed += '\n'; emit typedChanged(); break;
			case Qt::Key_Tab:       m_typed += '\t'; emit typedChanged(); break;
			default: break; // arrows / F-keys / etc. aren't echoed
		}
	}
	Q_INVOKABLE void sendSystemKey(int /*qtKey*/) { m_typed += "  [Win]  "; emit typedChanged(); }

	void clear() { m_typed.clear(); emit typedChanged(); }

signals:
	void keyboardLegendsChanged();
	void typedChanged();

private:
	QVariantMap m_legends;
	QString m_typed;
};
