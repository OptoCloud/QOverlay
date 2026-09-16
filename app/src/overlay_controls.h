#pragma once

#include <QObject>
#include <QString>

namespace QOverlay {

class WindowManager;

// Bridge exposed to a per-overlay control bar's QML as `ctl`. Each active capture overlay
// has one; it forwards the bar's actions (opacity, change source, delete) to the
// WindowManager for that overlay's id.
class OverlayControls : public QObject {
	Q_OBJECT
	Q_PROPERTY(QString title READ title NOTIFY titleChanged)
	Q_PROPERTY(QString kind READ kind CONSTANT)
	Q_PROPERTY(qreal opacity READ opacity WRITE setOpacity NOTIFY opacityChanged)

public:
	OverlayControls(WindowManager* manager, const QString& id, const QString& title, const QString& kind, QObject* parent = nullptr);

	QString title() const { return m_title; }
	QString kind() const { return m_kind; }
	qreal opacity() const { return m_opacity; }

	void setOpacity(qreal opacity);
	void setTitle(const QString& title);

	Q_INVOKABLE void deleteOverlay();
	Q_INVOKABLE void changeSource();

signals:
	void titleChanged();
	void opacityChanged();

private:
	WindowManager* m_manager;
	QString m_id;
	QString m_title;
	QString m_kind;
	qreal m_opacity = 1.0;
};

}
