#pragma once

#include <QObject>

namespace QOverlay {

class Config : public QObject {
	Q_OBJECT
	Q_DISABLE_COPY(Config)
public:
	static Config& Instance();

	bool Load();
	bool Save() const;

	bool LeftHanded() const { return m_leftHanded; }

	// Analog input thresholds (0..1). A trigger pull >= ClickThreshold registers a UI
	// click; a grip force >= GrabThreshold starts moving/grabbing an overlay.
	float ClickThreshold() const { return m_clickThreshold; }
	float GrabThreshold() const { return m_grabThreshold; }

	void SetLeftHanded(bool left);
	void SetClickThreshold(float threshold);
	void SetGrabThreshold(float threshold);

signals:
	void Changed();

private:
	Config() = default;

	QString configPath() const;

	bool m_leftHanded = true;       // true = panel on left hand, interact with right
	float m_clickThreshold = 0.5f;  // trigger pull fraction to register a click
	float m_grabThreshold = 0.15f;  // grip FORCE fraction to grab/move an overlay (Index FSR scale is small)
};

}
