#pragma once

#include <openvr.h>
#include <cstdint>

namespace QOverlay::VR {
// Wraps SteamVR's action-based input system (IVRInput). All controller input and
// haptic feedback flow through the action manifest in bindings/, so bindings are
// fully rebindable from the SteamVR controller binding UI.
struct Input {
	enum class Hand : std::uint8_t { Left, Right };

	// Loads the action manifest and resolves action/handle references. Call once
	// after VR_Init. Returns false if the input system is unavailable.
	static bool Initialize();

	// Pumps the active action set for the current frame. Must be called once per
	// frame before reading any action state.
	static void Update();

	// Click / grab state for the given hand. Uses the analog trigger/grip value
	// compared against the configurable Config thresholds when the analog action is
	// bound, falling back to the digital (SteamVR-thresholded) action otherwise.
	static bool ClickActive(Hand hand);
	static bool GrabActive(Hand hand);

	// Raw analog trigger pull / grip force for the given hand (0..1), or a fallback
	// derived from the digital action when no analog binding is present.
	static float TriggerValue(Hand hand);
	static float GripValue(Hand hand);

	// Thumbstick / trackpad position for the given hand, x/y in [-1,1]. Zeroed if the
	// action is unbound/inactive. Used to push/pull a grabbed overlay and to scroll the
	// hovered surface (y axis).
	static void Thumbstick(Hand hand, float& outX, float& outY);

	// True while the thumb rests on the pad/stick. Used as a modifier: a trigger pull with
	// the thumb touching becomes a right-click instead of a left-click. False if unbound.
	static bool ThumbTouchActive(Hand hand);

	// The controller's aim ("tip") pose for the given hand, in standing-universe space.
	// This is the manufacturer-defined pointing direction — use it for raycasting rather
	// than the raw grip pose, which is tilted from where the user intuitively points.
	// Returns false if unbound or the pose isn't valid this frame.
	static bool PointerPose(Hand hand, vr::HmdMatrix34_t& outPose);

	// Fires the haptic output action on the given hand.
	static void TriggerHaptic(Hand hand, float durationSeconds = 0.02f, float frequency = 200.0f, float amplitude = 0.5f);

private:
	static bool DigitalActive(vr::VRActionHandle_t action, Hand hand);
	// Reads an analog (vector1) action's x component. Returns false if the action is
	// unbound/inactive so callers can fall back to the digital action.
	static bool AnalogValue(vr::VRActionHandle_t action, Hand hand, float& out);
	static vr::VRInputValueHandle_t HandSource(Hand hand);
};
}
