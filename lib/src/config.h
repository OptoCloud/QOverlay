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

	// Smoothing amounts (0 = off/instant, 1 = maximum). Applied as an exponential moving
	// average: the pointer dot's on-screen motion (CursorSmoothing) and a grabbed overlay's
	// pose while dragging (DragSmoothing). Higher = steadier but laggier.
	float CursorSmoothing() const { return m_cursorSmoothing; }
	float DragSmoothing() const { return m_dragSmoothing; }

	void SetLeftHanded(bool left);
	void SetClickThreshold(float threshold);
	void SetGrabThreshold(float threshold);
	void SetCursorSmoothing(float amount);
	void SetDragSmoothing(float amount);

signals:
	void Changed();

private:
	Config() = default;

	QString configPath() const;

	bool m_leftHanded = true;       // true = panel on left hand, interact with right
	float m_clickThreshold = 0.5f;  // trigger pull fraction to register a click
	float m_grabThreshold = 0.15f;  // grip FORCE fraction to grab/move an overlay (Index FSR scale is small)
	float m_cursorSmoothing = 0.0f; // EMA amount for pointer motion (0 = off)
	float m_dragSmoothing = 0.0f;   // EMA amount for a grabbed overlay's pose (0 = off)
};

}
