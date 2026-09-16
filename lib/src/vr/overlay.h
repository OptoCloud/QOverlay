#pragma once

#include "tracked_device.h"
#include "transform.h"

#include <memory>

#include <QObject>
#include <QString>
#include <QPointF>
#include <QUrl>
#include <glm/vec2.hpp>
#include <openvr.h>

namespace QOverlay::VR {

class IOverlayScene;

class Overlay : public QObject {
	Q_OBJECT
	Q_DISABLE_COPY(Overlay)

	Overlay() = delete;
public:
	// Default: a QML-backed overlay (QmlOverlayScene created internally).
	Overlay(const QString& key, const QString& name, QObject* parent = nullptr);
	// Backed by a caller-supplied scene (e.g. a CaptureOverlayScene). The Overlay takes
	// ownership of `scene` and destroys it. `scene` need not be a QObject.
	Overlay(const QString& key, const QString& name, IOverlayScene* scene, QObject* parent = nullptr);
	~Overlay() override;

	bool Ok() const;
	bool Visible() const;
	float Width() const { return m_width; }
	glm::vec2 Size() const { return m_size; }
	IOverlayScene* Scene() const { return m_scene.get(); }
	vr::VROverlayHandle_t Handle() const { return m_handle; }

	// Load a .qml file as the overlay contents.
	void SetSource(const QUrl& source);

	// Expose a C++ object to the QML scene under `name` (a QML context property).
	// Must be called before SetSource so the loaded component can bind to it.
	void SetContextProperty(const QString& name, QObject* object);

	bool GetTransformAbsolute(Transform& transform) const;
	void SetTransformAbsolute(const Transform& transform);

	bool IsGrabbed() const { return m_grabbed; }
	bool Grabbable() const { return m_grabbable; }
	void SetGrabbable(bool grabbable) { m_grabbable = grabbable; }
	vr::TrackedDeviceIndex_t AttachedDevice() const { return m_attachedDevice; }
	void BeginGrab(vr::TrackedDeviceIndex_t controllerIndex, const Transform& controllerTransform);
	void UpdateGrab(const Transform& controllerTransform);
	void EndGrab();
	// Push the grabbed overlay along a world-space direction (the controller's aim ray, so
	// it slides along where you point — the same ray the pointer→screen raycast uses):
	// +metres farther, -metres closer. `controllerWorld` is the grabbing controller's world
	// transform. No-op unless grabbed.
	void PushGrabDistance(const glm::mat4& controllerWorld, const glm::vec3& worldDir, float meters);

	void AttachToDevice(TrackedDevice::DeviceType deviceType, const Transform& offset);
	void MirrorToDevice(TrackedDevice::DeviceType deviceType);

	// Re-level the overlay: keep its yaw + position but zero pitch/roll so it stands upright.
	// Called each tick while the "pitch level" option is on. No-op if it faces straight up/down.
	void LevelPitch();

	// Lock the overlay to follow a tracked device (head/controller), computing the offset from
	// its current world pose so it doesn't jump. Unlock() drops it back into world space where
	// it currently sits. IsLocked() reflects device attachment.
	void LockToDevice(TrackedDevice::DeviceType deviceType);
	void Unlock();
	bool IsLocked() const { return m_attachedDevice != vr::k_unTrackedDeviceIndexInvalid; }

	// Dim the overlay while it's grabbed (on) / restore it (off). The move/resize symbol
	// chip is a single shared overlay driven by the raycaster (see GrabIndicator).
	void SetGrabHighlight(bool on);
	float Opacity() const { return m_opacity; }

	// Compositor render order among overlapping overlays — higher draws on top. Regular
	// content overlays are 0; the grab chip is 200 and the pointer cursor 300 (PointerCursor).
	void SetSortOrder(uint32_t order);

public slots:
	void SetVisible(bool visible);
	void SetWidth(float width);
	// Overlay opacity 0..1 (IVROverlay::SetOverlayAlpha).
	void SetOpacity(float alpha);
	void SetTransformRelative(const Transform& offset, TrackedDevice::DeviceType deviceType);
signals:
	void VisibleChanged(bool visible);
	void WidthChanged(float width);
private:
	vr::VROverlayHandle_t m_handle;
	// Owned outright (not a QObject child), so an explicit smart pointer expresses and
	// enforces the lifetime the old raw pointer only managed by hand in the destructor.
	std::unique_ptr<IOverlayScene> m_scene;
	QString m_key;
	QString m_name;
	glm::vec2 m_size;
	float m_width;
	float m_opacity = 1.0f;
	bool m_grabbable = true;
	vr::TrackedDeviceIndex_t m_attachedDevice = vr::k_unTrackedDeviceIndexInvalid;
	Transform m_relativeOffset;

	// Grab state
	bool m_grabbed = false;
	glm::mat4 m_grabOffset;
	vr::TrackedDeviceIndex_t m_savedAttachedDevice = vr::k_unTrackedDeviceIndexInvalid;
	// Drag-smoothing EMA state: last emitted pose while grabbing (Config::DragSmoothing).
	glm::mat4 m_smoothedGrab = glm::mat4(1.0f);
	bool m_hasSmoothedGrab = false;
};
}
