#include "vr/overlay.h"

#include "vr/conversion.h"
#include "vr/qml_overlay_scene.h"
#include "vr/system.h"
#include "vr/tracked_device.h"

#include "log.h"

#include <algorithm>

#include <fmt/core.h>
#include <glm/geometric.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

QOverlay::VR::Overlay::Overlay(const QString& key, const QString& name, QObject* parent)
	: Overlay(key, name, new QmlOverlayScene(), parent)
{
}

QOverlay::VR::Overlay::Overlay(const QString& key, const QString& name, IOverlayScene* scene, QObject* parent)
	: QObject(parent)
	, m_handle(vr::k_ulOverlayHandleInvalid)
	, m_scene(scene)
	, m_key(key)
	, m_name(name)
	, m_size(1.0f, 1.0f)
	, m_width(1.0f)
{
	// VR Overlay
	auto overlay = vr::VROverlay();
	if (overlay == nullptr) {
		LOG_ERROR("Overlay '{}': failed to get IVROverlay interface", m_key.toStdString());
		return;
	}

	if (const vr::EVROverlayError error = overlay->CreateOverlay(m_key.toLatin1().data(), m_name.toLatin1().data(), &m_handle); error != vr::VROverlayError_None) {
		LOG_ERROR("Overlay '{}': CreateOverlay failed: {}", m_key.toStdString(), overlay->GetOverlayErrorNameFromEnum(error));
		m_handle = vr::k_ulOverlayHandleInvalid;
		return;
	}
	if (!m_scene->Ok()) {
		LOG_ERROR("Overlay '{}': scene not OK after CreateOverlay (GL/QML init failed?)", m_key.toStdString());
	}

	m_scene->SetHandle(m_handle);

	// Disable OpenVR's built-in input — we handle raycasting ourselves
	overlay->SetOverlayInputMethod(m_handle, vr::VROverlayInputMethod_None);

	// Register overlay
	VRSystem::RegisterOverlay(this);
}

QOverlay::VR::Overlay::~Overlay() {
	// Unregister overlay
	VRSystem::UnregisterOverlay(this);

	if (m_handle != vr::k_ulOverlayHandleInvalid) {
		auto overlay = vr::VROverlay();
		if (overlay == nullptr) {
			fmt::print("Failed to get IVROverlay interface\n");
		} else {
			overlay->DestroyOverlay(m_handle);
		}
	}

	// The scene is owned by us (not parented to a QObject); resetting the unique_ptr here —
	// after DestroyOverlay — keeps the same teardown order. The virtual IOverlayScene
	// destructor handles both QML and capture scenes.
	m_scene.reset();
}

bool QOverlay::VR::Overlay::Ok() const {
	return m_handle != vr::k_ulOverlayHandleInvalid && m_scene != nullptr && m_scene->Ok();
}

void QOverlay::VR::Overlay::SetSource(const QUrl& source) {
	auto* scene = dynamic_cast<QmlOverlayScene*>(m_scene.get());
	if (scene == nullptr) {
		fmt::print("Overlay::SetSource: scene is not a QmlOverlayScene\n");
		return;
	}
	scene->SetSource(source);
}

void QOverlay::VR::Overlay::SetContextProperty(const QString& name, QObject* object) {
	auto* scene = dynamic_cast<QmlOverlayScene*>(m_scene.get());
	if (scene == nullptr) {
		fmt::print("Overlay::SetContextProperty: scene is not a QmlOverlayScene\n");
		return;
	}
	scene->SetContextProperty(name, object);
}

bool QOverlay::VR::Overlay::Visible() const {
	const auto overlay = vr::VROverlay();
	if (overlay == nullptr) {
		fmt::print("Failed to get IVROverlay interface\n");
		return false;
	}

	return overlay->IsOverlayVisible(m_handle);
}

void QOverlay::VR::Overlay::SetVisible(bool visible) {
	const auto overlay = vr::VROverlay();
	if (overlay == nullptr) {
		fmt::print("Failed to get IVROverlay interface\n");
		return;
	}

	vr::EVROverlayError error;
	if (visible) {
		error = overlay->ShowOverlay(m_handle);
	} else {
		error = overlay->HideOverlay(m_handle);
	}

	if (error != vr::VROverlayError_None) {
		fmt::print("Failed to set overlay visibility: {}\n", overlay->GetOverlayErrorNameFromEnum(error));
		return;
	}

	emit VisibleChanged(visible);
}

void QOverlay::VR::Overlay::SetWidth(float width) {
	auto overlay = vr::VROverlay();
	if (overlay == nullptr) {
		fmt::print("Failed to get IVROverlay interface\n");
		return;
	}

	if (const vr::EVROverlayError error = overlay->SetOverlayWidthInMeters(m_handle, width); error != vr::VROverlayError_None) {
		fmt::print("Failed to set overlay width: {}\n", overlay->GetOverlayErrorNameFromEnum(error));
		return;
	}

	m_width = width;

	emit WidthChanged(width);
}

void QOverlay::VR::Overlay::SetOpacity(float alpha) {
	auto overlay = vr::VROverlay();
	if (overlay == nullptr) return;
	m_opacity = std::clamp(alpha, 0.0f, 1.0f);
	overlay->SetOverlayAlpha(m_handle, m_opacity);
}

void QOverlay::VR::Overlay::SetTransformRelative(const Transform& offset, TrackedDevice::DeviceType deviceType) {
	auto overlay = vr::VROverlay();
	if (overlay == nullptr) {
		fmt::print("Failed to get IVROverlay interface\n");
		return;
	}

	vr::TrackedDeviceIndex_t deviceIndex = TrackedDevice::GetIndex(deviceType);
	if (deviceIndex == vr::k_unTrackedDeviceIndexInvalid) {
		fmt::print("Failed to find tracked device\n");
		return;
	}

	m_attachedDevice = deviceIndex;
	m_relativeOffset = offset;

	vr::HmdMatrix34_t mat = offset.ToHmdMatrix();
	if (const vr::EVROverlayError error = overlay->SetOverlayTransformTrackedDeviceRelative(m_handle, deviceIndex, &mat); error != vr::VROverlayError_None) {
		fmt::print("Failed to set overlay transform: {}\n", overlay->GetOverlayErrorNameFromEnum(error));
	}
}

bool QOverlay::VR::Overlay::GetTransformAbsolute(Transform& transform) const {
	// If attached to a device, compute world transform from device pose * offset
	if (m_attachedDevice != vr::k_unTrackedDeviceIndexInvalid) {
		auto system = vr::VRSystem();
		if (system == nullptr) return false;

		std::array<vr::TrackedDevicePose_t, vr::k_unMaxTrackedDeviceCount> poses;
		system->GetDeviceToAbsoluteTrackingPose(vr::TrackingUniverseStanding, 0.0f, poses.data(), vr::k_unMaxTrackedDeviceCount);

		if (!poses[m_attachedDevice].bPoseIsValid) return false;

		glm::mat4 deviceMat = Conversion::ToGlmMat(poses[m_attachedDevice].mDeviceToAbsoluteTracking);
		glm::mat4 overlayMat = deviceMat * m_relativeOffset.ToGlmMatrix();
		transform = Transform(overlayMat);
		return true;
	}

	// Otherwise try the direct OpenVR call
	auto overlay = vr::VROverlay();
	if (overlay == nullptr) return false;

	vr::ETrackingUniverseOrigin origin;
	vr::HmdMatrix34_t mat;
	if (const vr::EVROverlayError error = overlay->GetOverlayTransformAbsolute(m_handle, &origin, &mat); error != vr::VROverlayError_None) {
		return false;
	}

	transform = Transform(mat);
	return true;
}

void QOverlay::VR::Overlay::SetTransformAbsolute(const Transform& transform) {
	auto overlay = vr::VROverlay();
	if (overlay == nullptr) return;

	vr::HmdMatrix34_t mat = transform.ToHmdMatrix();
	overlay->SetOverlayTransformAbsolute(m_handle, vr::TrackingUniverseStanding, &mat);

	// Clear device-relative attachment
	m_attachedDevice = vr::k_unTrackedDeviceIndexInvalid;
}

void QOverlay::VR::Overlay::SetGrabHighlight(bool on) {
	auto overlay = vr::VROverlay();
	if (overlay == nullptr) return;
	// Dim the held overlay's own texture (per-overlay); the shared symbol chip is handled
	// by GrabIndicator, driven from the raycaster.
	if (on) {
		overlay->SetOverlayColor(m_handle, 0.55f, 0.60f, 0.72f);
	} else {
		overlay->SetOverlayColor(m_handle, 1.0f, 1.0f, 1.0f);
	}
}

void QOverlay::VR::Overlay::BeginGrab(vr::TrackedDeviceIndex_t controllerIndex, const Transform& controllerTransform) {
	Transform overlayTransform;
	if (!GetTransformAbsolute(overlayTransform)) return;

	// Store the offset: inverse(grabbing controller) * overlay
	glm::mat4 controllerInverse = glm::inverse(controllerTransform.ToGlmMatrix());
	m_grabOffset = controllerInverse * overlayTransform.ToGlmMatrix();
	m_grabbed = true;
	SetGrabHighlight(true);

	// Save original attachment for restoration
	m_savedAttachedDevice = m_attachedDevice;
}

void QOverlay::VR::Overlay::UpdateGrab(const Transform& controllerTransform) {
	if (!m_grabbed) return;

	// New overlay transform = controller * offset
	glm::mat4 newOverlayMat = controllerTransform.ToGlmMatrix() * m_grabOffset;
	SetTransformAbsolute(Transform(newOverlayMat));
}

void QOverlay::VR::Overlay::PushGrabDistance(const glm::mat4& controllerWorld, const glm::vec3& worldDir, float meters) {
	if (!m_grabbed) return;
	// Slide the overlay along the aim ray in world space, then express that move in the
	// controller's local frame and add it to the grab offset so it stays attached while
	// moving along where you point.
	const glm::vec3 worldDelta = glm::normalize(worldDir) * meters;
	const glm::vec3 localDelta = glm::vec3(glm::inverse(controllerWorld) * glm::vec4(worldDelta, 0.0f));
	m_grabOffset[3] += glm::vec4(localDelta, 0.0f);
}

void QOverlay::VR::Overlay::EndGrab() {
	if (!m_grabbed) return;
	m_grabbed = false;
	SetGrabHighlight(false);

	// Re-attach to original controller with updated offset
	if (m_savedAttachedDevice != vr::k_unTrackedDeviceIndexInvalid) {
		auto system = vr::VRSystem();
		if (system == nullptr) return;

		std::array<vr::TrackedDevicePose_t, vr::k_unMaxTrackedDeviceCount> poses;
		system->GetDeviceToAbsoluteTrackingPose(vr::TrackingUniverseStanding, 0.0f, poses.data(), vr::k_unMaxTrackedDeviceCount);

		if (poses[m_savedAttachedDevice].bPoseIsValid) {
			glm::mat4 deviceMat = Conversion::ToGlmMat(poses[m_savedAttachedDevice].mDeviceToAbsoluteTracking);

			Transform overlayTransform;
			if (GetTransformAbsolute(overlayTransform)) {
				// Compute new relative offset: inverse(device) * overlay
				glm::mat4 newOffset = glm::inverse(deviceMat) * overlayTransform.ToGlmMatrix();
				m_relativeOffset = Transform(newOffset);
				m_attachedDevice = m_savedAttachedDevice;

				auto overlay = vr::VROverlay();
				if (overlay != nullptr) {
					vr::HmdMatrix34_t mat = m_relativeOffset.ToHmdMatrix();
					overlay->SetOverlayTransformTrackedDeviceRelative(m_handle, m_attachedDevice, &mat);
				}
			}
		}
	}
}

void QOverlay::VR::Overlay::AttachToDevice(TrackedDevice::DeviceType deviceType, const Transform& offset) {
	vr::TrackedDeviceIndex_t deviceIndex = TrackedDevice::GetIndex(deviceType);
	if (deviceIndex == vr::k_unTrackedDeviceIndexInvalid) return;

	m_attachedDevice = deviceIndex;
	m_relativeOffset = offset;

	auto overlay = vr::VROverlay();
	if (overlay != nullptr) {
		vr::HmdMatrix34_t mat = offset.ToHmdMatrix();
		overlay->SetOverlayTransformTrackedDeviceRelative(m_handle, deviceIndex, &mat);
	}
}

void QOverlay::VR::Overlay::MirrorToDevice(TrackedDevice::DeviceType deviceType) {
	vr::TrackedDeviceIndex_t newDeviceIndex = TrackedDevice::GetIndex(deviceType);
	if (newDeviceIndex == vr::k_unTrackedDeviceIndexInvalid) {
		fmt::print("MirrorToDevice: target device not found\n");
		return;
	}

	// Mirror the offset across the controller's YZ plane (negate X). Both hands share
	// the same local frame in OpenVR, so a true reflection — not a yaw — gives the
	// symmetric placement. Reflecting the orientation is R' = S * R * S with
	// S = diag(-1, 1, 1); this stays a proper rotation and keeps the overlay upright
	// (the old flipY * rot negated the up vector, flipping the panel upside down).
	glm::vec3 pos = m_relativeOffset.Position();
	pos.x = -pos.x;
	const glm::mat3 S = glm::mat3(-1.0f, 0.0f, 0.0f,
	                               0.0f, 1.0f, 0.0f,
	                               0.0f, 0.0f, 1.0f);
	glm::mat3 R = glm::mat3_cast(m_relativeOffset.Rotation());
	glm::quat rot = glm::quat_cast(S * R * S);
	m_relativeOffset = Transform(pos, rot);

	m_attachedDevice = newDeviceIndex;

	auto overlay = vr::VROverlay();
	if (overlay != nullptr) {
		vr::HmdMatrix34_t mat = m_relativeOffset.ToHmdMatrix();
		auto err = overlay->SetOverlayTransformTrackedDeviceRelative(m_handle, newDeviceIndex, &mat);
		if (err != vr::VROverlayError_None) {
			fmt::print("MirrorToDevice: SetOverlayTransformTrackedDeviceRelative failed: {}\n",
				overlay->GetOverlayErrorNameFromEnum(err));
		}
	}
}