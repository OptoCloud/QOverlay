#pragma once

#include <QSize>
#include <QtCore/qnamespace.h>

#include <glm/vec2.hpp>
#include <openvr.h>

namespace QOverlay::VR {

// Minimal scene contract the Overlay and the input raycaster depend on, so an
// Overlay can be backed by either the QWidget path (OverlayScene) or the QML path
// (QmlOverlayScene) interchangeably.
class IOverlayScene {
public:
	virtual ~IOverlayScene() = default;

	virtual bool Ok() const = 0;
	virtual bool Ready() const = 0;
	virtual vr::VROverlayHandle_t Handle() const = 0;
	virtual void SetHandle(vr::VROverlayHandle_t handle) = 0;
	virtual QSize Size() const = 0;

	// Pointer update for a given hand (0 = left, 1 = right). NoButton is hover-only.
	virtual bool FireMouseEvent(int hand, Qt::MouseButton button, const glm::vec2& pos) = 0;
	virtual void MouseNotPresent(int hand) = 0;

	// Whether the raycaster should float its per-hand VR cursor overlay over this scene.
	// QML scenes want it (they no longer draw their own dot); capture scenes don't — they
	// inject the real OS cursor, which the captured frame already shows.
	virtual bool ShowsVrCursor() const { return false; }
};
}
