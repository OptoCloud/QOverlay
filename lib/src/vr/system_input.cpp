#include "vr/system.h"

#include "config.h"
#include "log.h"
#include "vr/conversion.h"
#include "vr/grab_indicator.h"
#include "vr/pointer_cursor.h"
#include "vr/input.h"
#include "vr/math.h"
#include "vr/overlay.h"
#include "vr/ioverlay_scene.h"
#include "vr/tracked_device.h"
#include "vr/transform.h"

#include <openvr.h>
#include <glm/vec3.hpp>
#include <glm/vec2.hpp>
#include <glm/mat3x3.hpp>
#include <glm/mat4x4.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/quaternion.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <mutex>
#include <vector>

// s_overlays: all currently-registered overlays. Populated by VRSystem::RegisterOverlay
// (Overlay's constructor) and removed by UnregisterOverlay (Overlay's destructor, or
// WindowManager closing an overlay at runtime). Read every tick by PollInput() for
// raycasting. Guarded by s_overlaysMutex — defensively, since the VR poll/update path
// currently runs entirely on one thread (the main-thread QTimer in app/main.cpp) so the lock
// has zero contention today; it's here so a future multi-threaded change can't silently race.
static std::mutex s_overlaysMutex;
static std::vector<QOverlay::VR::Overlay*> s_overlays;

// Per-controller interaction state
struct ControllerState {
	QOverlay::VR::Overlay* hoveredOverlay = nullptr;
	QOverlay::VR::Overlay* grabbedOverlay = nullptr;
	// Mouse capture: the overlay a click started on. While set, the pointer keeps
	// routing to it (clamped to its edge) until the trigger releases, so a drag that
	// leaves the overlay border doesn't drop — the cursor rides the edge.
	QOverlay::VR::Overlay* pressedOverlay = nullptr;
	glm::vec2 capturedPixel = glm::vec2(0.0f);
	bool wasGripPressed = false;
	bool wasTriggerPressed = false;
	// Button chosen at the trigger's rising edge (left, or right if the thumb was on the pad)
	// and held for the whole press so lifting the thumb mid-press can't flip it.
	Qt::MouseButton pressButton = Qt::NoButton;
	// Fractional wheel-notch accumulator for thumbstick scrolling while hovering.
	float scrollAccum = 0.0f;
	// Exponential-moving-average state for cursor-motion smoothing (Config::CursorSmoothing).
	glm::vec2 cursorPixel = glm::vec2(0.0f);
	bool cursorValid = false;
};
static std::array<ControllerState, vr::k_unMaxTrackedDeviceCount> s_controllerState = {};

// Two-handed resize state. Captured once, the instant the second hand joins an existing
// one-handed grab on the same overlay (see "both hands grabbing the same overlay" in
// PollInput()). Everything is relative to that instant so the overlay doesn't teleport:
// only the *change* in hand separation (scale) and midpoint (translation) is applied;
// orientation stays fixed at the captured value. `overlay` is null when inactive; cleared on
// release (one hand remaining re-anchors via BeginGrab, see "leaving two-handed mode" below).
static struct {
	QOverlay::VR::Overlay* overlay = nullptr;
	float baseDistance = 0.0f;
	float baseWidth = 0.0f;
	glm::vec3 basePosition = glm::vec3(0.0f); // overlay position when two-handed began
	glm::vec3 baseMidpoint = glm::vec3(0.0f); // hands' midpoint when two-handed began
	glm::mat3 orientation = glm::mat3(1.0f);
} s_twoHanded;

void QOverlay::VR::VRSystem::RegisterOverlay(QOverlay::VR::Overlay* overlay)
{
	// See s_overlaysMutex declaration above for why this lock exists.
	std::lock_guard<std::mutex> lock(s_overlaysMutex);
	s_overlays.push_back(overlay);
}

void QOverlay::VR::VRSystem::UnregisterOverlay(QOverlay::VR::Overlay* overlay)
{
	std::lock_guard<std::mutex> lock(s_overlaysMutex);
	std::erase(s_overlays, overlay);

	// An overlay can be destroyed at runtime (window manager close). Drop any dangling
	// references the per-controller interaction state holds to it, or the next poll
	// would touch freed memory.
	for (auto& state : s_controllerState) {
		if (state.hoveredOverlay == overlay) state.hoveredOverlay = nullptr;
		if (state.grabbedOverlay == overlay) state.grabbedOverlay = nullptr;
		if (state.pressedOverlay == overlay) state.pressedOverlay = nullptr;
	}
	if (s_twoHanded.overlay == overlay) s_twoHanded.overlay = nullptr;
}

static std::array<vr::TrackedDevicePose_t, vr::k_unMaxTrackedDeviceCount> s_devices;
static std::array<vr::TrackedDevicePose_t, vr::k_unMaxTrackedDeviceCount> s_prevDevices;

// Projects the controller ray onto an overlay's plane and returns the hit position in
// texture pixels. Returns false if the ray is parallel/behind the plane. When `clamp`
// is set the UV is kept within [0,1] (the nearest edge point) instead of rejected —
// used while a drag is captured so the cursor sticks to the border.
static bool RayToOverlayPixel(QOverlay::VR::Overlay* overlay, const glm::mat4& overlayMat,
                              const glm::vec3& rayOrigin, const glm::vec3& rayDir,
                              bool clamp, glm::vec2& outPixel, float& outDistance)
{
	const QSize sceneSize = overlay->Scene()->Size();
	if (sceneSize.isEmpty()) return false;

	const glm::vec3 planeOrigin = glm::vec3(overlayMat[3]);
	const glm::vec3 planeNormal = -glm::vec3(overlayMat[2]);

	glm::vec3 intersect3D;
	glm::vec2 intersectUV;
	float distance;
	if (!QOverlay::VR::Math::IntersectRayPlane(rayOrigin, rayDir, planeOrigin, planeNormal, intersect3D, intersectUV, distance)) {
		return false;
	}

	const glm::vec3 localOffset = intersect3D - planeOrigin;
	const float overlayWidth = overlay->Width();
	const float aspect = static_cast<float>(sceneSize.height()) / static_cast<float>(sceneSize.width());
	const float overlayHeight = overlayWidth * aspect;

	const float localX = glm::dot(localOffset, glm::vec3(overlayMat[0]));
	const float localY = glm::dot(localOffset, glm::vec3(overlayMat[1]));

	float u = (localX / overlayWidth) + 0.5f;
	float v = 1.0f - ((localY / overlayHeight) + 0.5f);

	if (clamp) {
		u = std::clamp(u, 0.0f, 1.0f);
		v = std::clamp(v, 0.0f, 1.0f);
	} else if (u < 0.0f || u > 1.0f || v < 0.0f || v > 1.0f) {
		return false;
	}

	outPixel = glm::vec2(u * static_cast<float>(sceneSize.width()), v * static_cast<float>(sceneSize.height()));
	outDistance = distance;
	return true;
}

// Inverse of RayToOverlayPixel's UV mapping: map a scene-pixel coordinate back to the
// world-space point on the overlay's plane. Used to place the per-hand VR cursor overlay at
// the (possibly edge-clamped) pointer position, so the dot rides exactly where the pixel is.
static glm::vec3 PixelToWorld(QOverlay::VR::Overlay* overlay, const glm::mat4& overlayMat, const glm::vec2& pixel)
{
	const glm::vec3 planeOrigin = glm::vec3(overlayMat[3]);
	const QSize sceneSize = overlay->Scene()->Size();
	if (sceneSize.isEmpty()) return planeOrigin;

	const float overlayWidth = overlay->Width();
	const float aspect = static_cast<float>(sceneSize.height()) / static_cast<float>(sceneSize.width());
	const float overlayHeight = overlayWidth * aspect;

	const float u = pixel.x / static_cast<float>(sceneSize.width());
	const float v = pixel.y / static_cast<float>(sceneSize.height());
	const float localX = (u - 0.5f) * overlayWidth;
	const float localY = (0.5f - v) * overlayHeight;

	return planeOrigin + localX * glm::vec3(overlayMat[0]) + localY * glm::vec3(overlayMat[1]);
}

void OnDeviceConnected(std::uint32_t index, const vr::TrackedDevicePose_t& pose)
{
	auto deviceClass = vr::VRSystem()->GetTrackedDeviceClass(index);
	switch (deviceClass)
	{
		case vr::TrackedDeviceClass_Invalid:
			LOG_INFO("[DEVICE #{:0>2}] Invalid device class connected", index);
			return;
		case vr::TrackedDeviceClass_HMD:
			LOG_INFO("[DEVICE #{:0>2}] HMD connected", index);
			break;
		case vr::TrackedDeviceClass_Controller:
			LOG_INFO("[DEVICE #{:0>2}] Controller connected", index);
			break;
		case vr::TrackedDeviceClass_GenericTracker:
			LOG_INFO("[DEVICE #{:0>2}] GenericTracker connected", index);
			break;
		case vr::TrackedDeviceClass_TrackingReference:
			LOG_INFO("[DEVICE #{:0>2}] TrackingReference connected", index);
			break;
		case vr::TrackedDeviceClass_DisplayRedirect:
			LOG_INFO("[DEVICE #{:0>2}] DisplayRedirect connected", index);
			break;
		default:
			LOG_INFO("[DEVICE #{:0>2}] Unknown device class connected", index);
			return;
	}
}
void OnDeviceDisconnected(std::uint32_t index)
{
	LOG_INFO("[DEVICE #{:0>2}] Disconnected", index);
}
void OnDevicePoseValidated(std::uint32_t index, const vr::TrackedDevicePose_t& pose)
{
	LOG_INFO("[DEVICE #{:0>2}] Pose validated", index);
}
void OnDevicePoseInvalidated(std::uint32_t index)
{
	LOG_INFO("[DEVICE #{:0>2}] Pose invalidated", index);
}
void OnDevicePoseUpdated(std::uint32_t index, const vr::TrackedDevicePose_t& pose)
{
}

static void ProcessControllerInteraction(QOverlay::VR::Input::Hand hand, std::uint32_t controllerIndex)
{
	auto& state = s_controllerState[controllerIndex];
	const int handIdx = (hand == QOverlay::VR::Input::Hand::Left) ? 0 : 1;

	// Drive this hand's VR cursor overlay: float it over cursor-drawing (QML) overlays at the
	// pointer hit, hide it otherwise. Cheap SetOverlayTransformAbsolute per poll, zero QML
	// re-render. `mat` is the hit overlay's absolute transform; `pixel` the (possibly clamped)
	// scene-pixel hit.
	auto showCursor = [&](QOverlay::VR::Overlay* ov, const glm::mat4& mat, const glm::vec2& pixel, bool pressed) {
		if (ov != nullptr && ov->Scene() != nullptr && ov->Scene()->ShowsVrCursor()) {
			QOverlay::VR::PointerCursor::Instance().Show(handIdx, mat, PixelToWorld(ov, mat, pixel), pressed);
		} else {
			QOverlay::VR::PointerCursor::Instance().Hide(handIdx);
		}
	};
	auto hideCursor = [&]() { QOverlay::VR::PointerCursor::Instance().Hide(handIdx); };

	const auto& pose = s_devices[controllerIndex];
	if (!pose.bPoseIsValid) { hideCursor(); return; }

	glm::mat4 controllerMat = QOverlay::VR::Conversion::ToGlmMat(pose.mDeviceToAbsoluteTracking);
	QOverlay::VR::Transform controllerTransform(controllerMat);

	// Cast the pointer ray from the controller's aim ("tip") pose so it matches where
	// the user intuitively points. Fall back to the grip pose's -Z if the aim pose is
	// unbound. Grabbing still uses the grip pose (controllerTransform) — that's the
	// natural anchor for holding the overlay.
	glm::vec3 rayOrigin;
	glm::vec3 rayDirection;
	vr::HmdMatrix34_t aimPose;
	if (QOverlay::VR::Input::PointerPose(hand, aimPose)) {
		glm::mat4 aimMat = QOverlay::VR::Conversion::ToGlmMat(aimPose);
		rayOrigin = glm::vec3(aimMat[3]);
		rayDirection = -glm::vec3(aimMat[2]);
	} else {
		rayOrigin = glm::vec3(controllerMat[3]);
		rayDirection = -glm::vec3(controllerMat[2]);
	}

	// Read controller buttons via the action-based input system (rebindable in SteamVR)
	bool triggerPressed = QOverlay::VR::Input::ClickActive(hand);
	bool gripPressed = QOverlay::VR::Input::GrabActive(hand);

	// Decide the effective mouse button for this press cycle at the trigger's rising edge:
	// thumb resting on the pad/stick → right-click, otherwise left. Latched in pressButton so
	// it stays consistent until release; clickButton is NoButton whenever the trigger is up.
	if (triggerPressed && !state.wasTriggerPressed) {
		state.pressButton = QOverlay::VR::Input::ThumbTouchActive(hand) ? Qt::RightButton : Qt::LeftButton;
	} else if (!triggerPressed) {
		state.pressButton = Qt::NoButton;
	}
	const Qt::MouseButton clickButton = triggerPressed ? state.pressButton : Qt::NoButton;

	// Debug: log grip press with the analog grip value that triggered it
	if (gripPressed && !state.wasGripPressed) {
		LOG_INFO("[CTRL #{:0>2}] Grip pressed (gripValue={:.3f}, hovering: {})",
			controllerIndex, QOverlay::VR::Input::GripValue(hand), state.hoveredOverlay != nullptr ? "yes" : "no");
	}

	// Debug (calibration): throttled live readout of grip force while squeezing, so we
	// can see the FSR's usable range and pick a sensible grabThreshold.
	{
		const float gv = QOverlay::VR::Input::GripValue(hand);
		static int s_gripDbg = 0;
		if (gv > 0.02f && (++s_gripDbg % 15 == 0)) {
			LOG_INFO("[CTRL #{:0>2}] gripValue={:.3f} (threshold={:.2f})",
				controllerIndex, gv, QOverlay::Config::Instance().GrabThreshold());
		}
	}

	// Handle active grab
	if (state.grabbedOverlay != nullptr) {
		if (gripPressed) {
			if (s_twoHanded.overlay == state.grabbedOverlay) {
				// Two-handed: the combined position/size is applied in PollInput after both
				// hands are processed — don't fight it with a per-hand rigid update here.
			} else {
				// Thumbstick Y pushes/pulls the grabbed overlay along the aim ray (the same
				// ray the pointer→screen raycast uses).
				float tx = 0.0f, ty = 0.0f;
				QOverlay::VR::Input::Thumbstick(hand, tx, ty);
				if (std::fabs(ty) > 0.2f) {
					state.grabbedOverlay->PushGrabDistance(controllerMat, rayDirection, ty * 0.02f);
				}
				state.grabbedOverlay->UpdateGrab(controllerTransform);
			}
			hideCursor(); // no pointer dot while grabbing
			state.wasGripPressed = gripPressed;
			state.wasTriggerPressed = triggerPressed;
			return; // Don't process hover/click while grabbing
		} else {
			state.grabbedOverlay->EndGrab();
			state.grabbedOverlay = nullptr;
		}
	}

	// Mouse capture: once a click started on an overlay, keep routing the pointer to it
	// (clamped to its edge) until the trigger releases — so dragging a slider past the
	// border keeps tracking and the cursor waits at the edge where you'll re-enter.
	if (state.pressedOverlay != nullptr) {
		QOverlay::VR::Overlay* captured = state.pressedOverlay;
		QOverlay::VR::Transform capturedTransform;
		if (captured->Ok() && captured->Visible() && !captured->IsGrabbed() && captured->GetTransformAbsolute(capturedTransform)) {
			glm::vec2 pixel;
			float distance;
			if (RayToOverlayPixel(captured, capturedTransform.ToGlmMatrix(), rayOrigin, rayDirection, true, pixel, distance)) {
				state.capturedPixel = pixel; // else: ray parallel/behind — hold last edge position
			}

			captured->Scene()->FireMouseEvent(handIdx, clickButton, state.capturedPixel);
			showCursor(captured, capturedTransform.ToGlmMatrix(), state.capturedPixel, triggerPressed);
			state.hoveredOverlay = captured;

			if (!triggerPressed) {
				state.pressedOverlay = nullptr; // release delivered above — end capture
			}
			state.wasGripPressed = gripPressed;
			state.wasTriggerPressed = triggerPressed;
			return;
		}
		state.pressedOverlay = nullptr; // captured overlay went away
	}

	// Raycast against overlays
	QOverlay::VR::Overlay* hitOverlay = nullptr;
	glm::vec2 hitPixel;
	glm::mat4 hitMat(1.0f); // hit overlay's absolute transform, for placing the cursor overlay
	float closestDistance = std::numeric_limits<float>::max();

	// Snapshot under lock rather than holding the mutex through the whole raycast loop below
	// (which calls into Overlay methods we can't prove are lock-free w.r.t. this mutex).
	std::vector<QOverlay::VR::Overlay*> overlaysSnapshot;
	{
		std::lock_guard<std::mutex> lock(s_overlaysMutex);
		overlaysSnapshot = s_overlays;
	}

	for (auto* overlay : overlaysSnapshot) {
		// Grabbed overlays stay hittable so the OTHER controller can grab them too (for the
		// two-handed resize); hover/click on a grabbed overlay is suppressed below.
		if (!overlay->Ok() || !overlay->Visible()) continue;
		if (overlay->AttachedDevice() == controllerIndex) continue; // Can't interact with own overlay

		QOverlay::VR::Transform overlayTransform;
		if (!overlay->GetTransformAbsolute(overlayTransform)) continue;

		glm::vec2 pixel;
		float distance;
		if (!RayToOverlayPixel(overlay, overlayTransform.ToGlmMatrix(), rayOrigin, rayDirection, false, pixel, distance)) {
			continue;
		}

		if (distance < closestDistance) {
			closestDistance = distance;
			hitOverlay = overlay;
			hitPixel = pixel;
			hitMat = overlayTransform.ToGlmMatrix();
		}
	}

	// Hover enter/leave haptics
	if (hitOverlay != state.hoveredOverlay) {
		if (hitOverlay != nullptr) {
			// Hover enter — light haptic
			QOverlay::VR::Input::TriggerHaptic(hand, 0.01f, 200.0f, 0.3f);
		}
		if (state.hoveredOverlay != nullptr) {
			state.hoveredOverlay->Scene()->MouseNotPresent(handIdx);
		}
	}

	if (hitOverlay != nullptr) {
		// Grip press — begin grab (also lets the second hand join a one-handed grab to
		// start a two-handed resize). Non-grabbable overlays (e.g. control bars) fall
		// through to normal hover/click.
		if (gripPressed && !state.wasGripPressed && hitOverlay->Grabbable()) {
			hitOverlay->Scene()->MouseNotPresent(handIdx);
			hitOverlay->BeginGrab(controllerIndex, controllerTransform);
			state.grabbedOverlay = hitOverlay;
			QOverlay::VR::Input::TriggerHaptic(hand, 0.05f, 200.0f, 1.0f);
			state.hoveredOverlay = nullptr;
			hideCursor(); // now grabbing, not pointing
		} else if (!hitOverlay->IsGrabbed()) {
			// Normal hover/click — only when the overlay isn't already being grabbed (by the
			// other hand), so the two hands don't fight over it.

			// Cursor smoothing: exponentially blend toward the raw hit so the dot glides instead
			// of jittering. Reset on a fresh hover (or a big jump) so it doesn't slide in from the
			// last position. Skipped while the trigger is down so clicks land exactly where aimed.
			const float cs = QOverlay::Config::Instance().CursorSmoothing();
			if (cs > 0.0f && !triggerPressed) {
				// A "jump" resets smoothing instead of blending toward the raw hit (so a
				// deliberate re-aim across the overlay doesn't glide). Scale the threshold to
				// the overlay's own pixel size rather than a fixed 200px, since that's ~39% of
				// a 512px scene but under 5% of a 4K one.
				const QSize hitSceneSize = hitOverlay->Scene()->Size();
				const float sceneDiagonal = glm::length(glm::vec2(
					static_cast<float>(hitSceneSize.width()), static_cast<float>(hitSceneSize.height())));
				constexpr float kJumpFractionOfDiagonal = 0.1f;
				const bool jumped = glm::distance(hitPixel, state.cursorPixel) > sceneDiagonal * kJumpFractionOfDiagonal;
				if (state.cursorValid && state.hoveredOverlay == hitOverlay && !jumped) {
					hitPixel = glm::mix(hitPixel, state.cursorPixel, cs);
				}
			}
			state.cursorPixel = hitPixel;
			state.cursorValid = true;

			hitOverlay->Scene()->FireMouseEvent(handIdx, clickButton, hitPixel);
			showCursor(hitOverlay, hitMat, hitPixel, triggerPressed);

			// Thumbstick Y scrolls the hovered surface while NOT clicking (a click means a
			// press/drag, not a scroll). Push up = scroll up; held = repeat, rate ∝ deflection.
			// The fractional accumulator turns the per-poll deflection into whole wheel notches.
			if (!triggerPressed) {
				float tx = 0.0f, ty = 0.0f;
				QOverlay::VR::Input::Thumbstick(hand, tx, ty);
				if (std::fabs(ty) > 0.2f) {
					constexpr float kScrollRate = 0.15f; // notches accrued per poll at full deflection
					state.scrollAccum += ty * kScrollRate;
					const int notches = static_cast<int>(state.scrollAccum);
					if (notches != 0) {
						state.scrollAccum -= static_cast<float>(notches);
						hitOverlay->Scene()->FireScroll(handIdx, hitPixel, notches);
					}
				} else {
					state.scrollAccum = 0.0f;
				}
			}

			// Trigger press start — haptic + begin mouse capture on this overlay.
			if (triggerPressed && !state.wasTriggerPressed) {
				QOverlay::VR::Input::TriggerHaptic(hand, 0.02f, 200.0f, 0.7f);
				state.pressedOverlay = hitOverlay;
				state.capturedPixel = hitPixel;
			}
		} else {
			hideCursor(); // hovering an overlay the other hand is grabbing — no dot
		}

		state.hoveredOverlay = hitOverlay;
	} else {
		if (state.hoveredOverlay != nullptr) {
			state.hoveredOverlay->Scene()->MouseNotPresent(handIdx);
		}
		state.hoveredOverlay = nullptr;
		state.scrollAccum = 0.0f;   // not hovering anything — drop any partial scroll notch
		state.cursorValid = false;  // and restart cursor smoothing on the next hover
		hideCursor(); // not pointing at anything
	}

	state.wasGripPressed = gripPressed;
	state.wasTriggerPressed = triggerPressed;
}

void QOverlay::VR::VRSystem::PollInput()
{
	auto system = vr::VRSystem();
	if (!system) return;

	// Pump the action set for this frame before reading any action state.
	Input::Update();

	constexpr std::size_t NDEVICES = vr::k_unMaxTrackedDeviceCount;

	s_prevDevices = s_devices;

	system->GetDeviceToAbsoluteTrackingPose(vr::TrackingUniverseStanding, 0.0f, s_devices.data(), NDEVICES);

	for (std::uint32_t i = 0; i < NDEVICES; i++)
	{
		const auto& pose = s_devices[i];
		const auto& prevPose = s_prevDevices[i];

		if (prevPose.bDeviceIsConnected != pose.bDeviceIsConnected)
		{
			if (pose.bDeviceIsConnected)
			{
				OnDeviceConnected(i, pose);
			}
			else
			{
				OnDevicePoseInvalidated(i);
				OnDeviceDisconnected(i);
			}
		}

		if (!pose.bDeviceIsConnected) continue;

		if (prevPose.bPoseIsValid != pose.bPoseIsValid)
		{
			if (pose.bPoseIsValid)
			{
				OnDevicePoseValidated(i, pose);
			}
			else
			{
				OnDevicePoseInvalidated(i);
				continue;
			}
		}

		OnDevicePoseUpdated(i, pose);
	}

	// Process controller interaction with overlays, driven per-hand so each hand's
	// action state maps to its physical controller.
	struct HandBinding {
		Input::Hand hand;
		TrackedDevice::DeviceType deviceType;
	};
	static constexpr HandBinding hands[] = {
		{ Input::Hand::Left, TrackedDevice::DeviceType::ControllerLeft },
		{ Input::Hand::Right, TrackedDevice::DeviceType::ControllerRight },
	};

	for (const auto& binding : hands)
	{
		vr::TrackedDeviceIndex_t index = TrackedDevice::GetIndex(binding.deviceType);
		if (index == vr::k_unTrackedDeviceIndexInvalid || index >= NDEVICES) continue;
		if (!s_devices[index].bDeviceIsConnected || !s_devices[index].bPoseIsValid) continue;

		ProcessControllerInteraction(binding.hand, index);
	}

	// Two-handed resize: if both controllers are grabbing the SAME overlay, scale its width
	// by how much the distance between the controllers has changed since the grab began
	// (spread apart = larger, pinch together = smaller). Position still follows the per-hand
	// grab above; this only drives size.
	{
		const vr::TrackedDeviceIndex_t lIdx = TrackedDevice::GetIndex(TrackedDevice::DeviceType::ControllerLeft);
		const vr::TrackedDeviceIndex_t rIdx = TrackedDevice::GetIndex(TrackedDevice::DeviceType::ControllerRight);
		// Per-hand grab state must not depend on the OTHER hand's pose validity — otherwise a
		// momentary tracking loss on one controller mid-resize would incorrectly clear the
		// still-grabbing hand's overlay below, skipping the re-anchor step meant to prevent a
		// jump (see the "leaving two-handed mode" branch).
		QOverlay::VR::Overlay* lo = (lIdx < NDEVICES) ? s_controllerState[lIdx].grabbedOverlay : nullptr;
		QOverlay::VR::Overlay* ro = (rIdx < NDEVICES) ? s_controllerState[rIdx].grabbedOverlay : nullptr;
		const bool bothPosesValid = lIdx < NDEVICES && rIdx < NDEVICES
			&& s_devices[lIdx].bPoseIsValid && s_devices[rIdx].bPoseIsValid;

		if (bothPosesValid && lo != nullptr && lo == ro) {
			const glm::vec3 lp = glm::vec3(QOverlay::VR::Conversion::ToGlmMat(s_devices[lIdx].mDeviceToAbsoluteTracking)[3]);
			const glm::vec3 rp = glm::vec3(QOverlay::VR::Conversion::ToGlmMat(s_devices[rIdx].mDeviceToAbsoluteTracking)[3]);
			const float dist = glm::length(lp - rp);
			const glm::vec3 mid = (lp + rp) * 0.5f;

			if (s_twoHanded.overlay != lo) {
				s_twoHanded.overlay = lo;
				s_twoHanded.baseDistance = std::max(dist, 0.02f);
				s_twoHanded.baseWidth = lo->Width();
				s_twoHanded.baseMidpoint = mid;
				QOverlay::VR::Transform current;
				if (lo->GetTransformAbsolute(current)) {
					s_twoHanded.basePosition = current.Position();
					s_twoHanded.orientation = glm::mat3_cast(current.Rotation());
				} else {
					s_twoHanded.basePosition = mid;
					s_twoHanded.orientation = glm::mat3(1.0f);
				}
			}

			// Width scales with the change in separation; position moves by the change in the
			// hands' midpoint — both RELATIVE to grab start, so nothing jumps on join.
			const float scale = dist / s_twoHanded.baseDistance;
			lo->SetWidth(std::clamp(s_twoHanded.baseWidth * scale, 0.1f, 8.0f));

			const glm::vec3 pos = s_twoHanded.basePosition + (mid - s_twoHanded.baseMidpoint);
			glm::mat4 transform(s_twoHanded.orientation);
			transform[3] = glm::vec4(pos, 1.0f);
			lo->SetTransformAbsolute(QOverlay::VR::Transform(transform));
		} else if (s_twoHanded.overlay != nullptr) {
			// Leaving two-handed mode: re-anchor whichever hand still holds the overlay so its
			// grab offset matches the new size/position (prevents a jump on release).
			QOverlay::VR::Overlay* remaining = lo ? lo : ro;
			const vr::TrackedDeviceIndex_t idx = lo ? lIdx : rIdx;
			if (remaining != nullptr && idx < NDEVICES && s_devices[idx].bDeviceIsConnected && s_devices[idx].bPoseIsValid) {
				QOverlay::VR::Transform t(QOverlay::VR::Conversion::ToGlmMat(s_devices[idx].mDeviceToAbsoluteTracking));
				remaining->BeginGrab(idx, t);
			} else if (remaining != nullptr) {
				// Device disconnected or its pose went invalid right as two-handed mode ended —
				// don't re-anchor onto stale/garbage pose data; just drop the grab outright so
				// UpdateGrab() isn't fed an offset derived from an invalid device.
				remaining->EndGrab();
				for (auto& st : s_controllerState) {
					if (st.grabbedOverlay == remaining) st.grabbedOverlay = nullptr;
				}
			}
			s_twoHanded.overlay = nullptr;
		}
	}

	// Drive the single shared grab indicator: show it over whatever is grabbed (resize glyph
	// when two-handed), otherwise hide it.
	{
		QOverlay::VR::Overlay* grabbed = s_twoHanded.overlay;
		const bool scaling = grabbed != nullptr;
		if (grabbed == nullptr) {
			for (const auto& st : s_controllerState) {
				if (st.grabbedOverlay != nullptr) { grabbed = st.grabbedOverlay; break; }
			}
		}
		QOverlay::VR::Transform t;
		if (grabbed != nullptr && grabbed->GetTransformAbsolute(t)) {
			QOverlay::VR::GrabIndicator::Instance().Show(t.ToGlmMatrix(), scaling);
		} else {
			QOverlay::VR::GrabIndicator::Instance().Hide();
		}
	}
}
