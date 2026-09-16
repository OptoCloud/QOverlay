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
	Q_PROPERTY(bool pitchLevel READ pitchLevel WRITE setPitchLevel NOTIFY pitchLevelChanged)
	Q_PROPERTY(QString lockTarget READ lockTarget WRITE setLockTarget NOTIFY lockTargetChanged)
	// Which drop-up the shared popup is currently showing for THIS overlay ("", "options",
	// "sources") — the bar highlights the matching button. Driven by WindowManager.
	Q_PROPERTY(QString openPanel READ openPanel NOTIFY openPanelChanged)

public:
	OverlayControls(WindowManager* manager, const QString& id, const QString& title, const QString& kind, QObject* parent = nullptr);

	QString title() const { return m_title; }
	QString kind() const { return m_kind; }
	qreal opacity() const { return m_opacity; }
	bool pitchLevel() const { return m_pitchLevel; }
	QString lockTarget() const { return m_lockTarget; }
	QString openPanel() const { return m_openPanel; }
	QString id() const { return m_id; }

	void setOpacity(qreal opacity);
	void setPitchLevel(bool level);
	void setLockTarget(const QString& target);
	void setTitle(const QString& title);
	// Reflect the shared popup's state onto this overlay (called by WindowManager).
	void setOpenPanel(const QString& panel);

	Q_INVOKABLE void deleteOverlay();
	// Retarget this overlay to a specific available surface (chosen from the Sources panel).
	Q_INVOKABLE void setSource(const QString& surfaceId);
	// Bar buttons: toggle the shared popup to this overlay's Options / Sources panel, or close.
	Q_INVOKABLE void openOptions();
	Q_INVOKABLE void openSources();
	Q_INVOKABLE void closePanel();

signals:
	void titleChanged();
	void opacityChanged();
	void pitchLevelChanged();
	void lockTargetChanged();
	void openPanelChanged();

private:
	WindowManager* m_manager;
	QString m_id;
	QString m_title;
	QString m_kind;
	qreal m_opacity = 1.0;
	bool m_pitchLevel = false;
	QString m_lockTarget = QStringLiteral("off");
	QString m_openPanel; // "", "options", "sources"
};

}
