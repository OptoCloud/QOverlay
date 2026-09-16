#include "vr/input.h"

#include "config.h"
#include "vr/schmitt.h"

#include <algorithm>

#include <QCoreApplication>
#include <QDir>

#include <fmt/core.h>

namespace {
vr::VRActionSetHandle_t s_actionSet = vr::k_ulInvalidActionSetHandle;
vr::VRActionHandle_t s_clickAction = vr::k_ulInvalidActionHandle;
vr::VRActionHandle_t s_grabAction = vr::k_ulInvalidActionHandle;
vr::VRActionHandle_t s_triggerValueAction = vr::k_ulInvalidActionHandle;
vr::VRActionHandle_t s_gripValueAction = vr::k_ulInvalidActionHandle;
vr::VRActionHandle_t s_pointerPoseAction = vr::k_ulInvalidActionHandle;
vr::VRActionHandle_t s_thumbstickAction = vr::k_ulInvalidActionHandle;
vr::VRActionHandle_t s_hapticAction = vr::k_ulInvalidActionHandle;
vr::VRInputValueHandle_t s_leftHand = vr::k_ulInvalidInputValueHandle;
vr::VRInputValueHandle_t s_rightHand = vr::k_ulInvalidInputValueHandle;
}

bool QOverlay::VR::Input::Initialize() {
	auto input = vr::VRInput();
	if (input == nullptr) {
		fmt::print("Failed to get IVRInput interface\n");
		return false;
	}

	const QString manifestPath = QDir(QCoreApplication::applicationDirPath()).filePath("bindings/qoverlay_actions.json");
	if (const vr::EVRInputError error = input->SetActionManifestPath(manifestPath.toUtf8().constData()); error != vr::VRInputError_None) {
		fmt::print("Failed to set action manifest path ({}): error {}\n", manifestPath.toStdString(), static_cast<int>(error));
		return false;
	}

	bool ok = true;
	auto getAction = [&](const char* path, vr::VRActionHandle_t& handle) {
		if (const vr::EVRInputError error = input->GetActionHandle(path, &handle); error != vr::VRInputError_None) {
			fmt::print("Failed to get action handle for {}: error {}\n", path, static_cast<int>(error));
			ok = false;
		}
	};

	if (const vr::EVRInputError error = input->GetActionSetHandle("/actions/main", &s_actionSet); error != vr::VRInputError_None) {
		fmt::print("Failed to get action set handle: error {}\n", static_cast<int>(error));
		ok = false;
	}
	getAction("/actions/main/in/Click", s_clickAction);
	getAction("/actions/main/in/Grab", s_grabAction);
	getAction("/actions/main/in/TriggerValue", s_triggerValueAction);
	getAction("/actions/main/in/GripValue", s_gripValueAction);
	getAction("/actions/main/in/Pointer", s_pointerPoseAction);
	getAction("/actions/main/in/Thumbstick", s_thumbstickAction);
	getAction("/actions/main/out/Haptic", s_hapticAction);

	if (const vr::EVRInputError error = input->GetInputSourceHandle("/user/hand/left", &s_leftHand); error != vr::VRInputError_None) {
		fmt::print("Failed to get left hand source handle: error {}\n", static_cast<int>(error));
		ok = false;
	}
	if (const vr::EVRInputError error = input->GetInputSourceHandle("/user/hand/right", &s_rightHand); error != vr::VRInputError_None) {
		fmt::print("Failed to get right hand source handle: error {}\n", static_cast<int>(error));
		ok = false;
	}

	return ok;
}

void QOverlay::VR::Input::Update() {
	auto input = vr::VRInput();
	if (input == nullptr || s_actionSet == vr::k_ulInvalidActionSetHandle) return;

	vr::VRActiveActionSet_t activeSet = {};
	activeSet.ulActionSet = s_actionSet;
	activeSet.ulRestrictedToDevice = vr::k_ulInvalidInputValueHandle;

	if (const vr::EVRInputError error = input->UpdateActionState(&activeSet, sizeof(activeSet), 1); error != vr::VRInputError_None) {
		fmt::print("UpdateActionState failed: error {}\n", static_cast<int>(error));
	}
}

vr::VRInputValueHandle_t QOverlay::VR::Input::HandSource(Hand hand) {
	return hand == Hand::Left ? s_leftHand : s_rightHand;
}

bool QOverlay::VR::Input::DigitalActive(vr::VRActionHandle_t action, Hand hand) {
	auto input = vr::VRInput();
	if (input == nullptr || action == vr::k_ulInvalidActionHandle) return false;

	vr::InputDigitalActionData_t data = {};
	if (input->GetDigitalActionData(action, &data, sizeof(data), HandSource(hand)) != vr::VRInputError_None) {
		return false;
	}

	return data.bActive && data.bState;
}

bool QOverlay::VR::Input::AnalogValue(vr::VRActionHandle_t action, Hand hand, float& out) {
	auto input = vr::VRInput();
	if (input == nullptr || action == vr::k_ulInvalidActionHandle) return false;

	vr::InputAnalogActionData_t data = {};
	if (input->GetAnalogActionData(action, &data, sizeof(data), HandSource(hand)) != vr::VRInputError_None) {
		return false;
	}
	if (!data.bActive) return false;

	out = data.x;
	return true;
}

float QOverlay::VR::Input::TriggerValue(Hand hand) {
	float value = 0.0f;
	if (AnalogValue(s_triggerValueAction, hand, value)) return value;
	// No analog binding — approximate from the digital click action.
	return DigitalActive(s_clickAction, hand) ? 1.0f : 0.0f;
}

float QOverlay::VR::Input::GripValue(Hand hand) {
	float value = 0.0f;
	if (AnalogValue(s_gripValueAction, hand, value)) return value;
	return DigitalActive(s_grabAction, hand) ? 1.0f : 0.0f;
}

void QOverlay::VR::Input::Thumbstick(Hand hand, float& outX, float& outY) {
	outX = 0.0f;
	outY = 0.0f;
	auto input = vr::VRInput();
	if (input == nullptr || s_thumbstickAction == vr::k_ulInvalidActionHandle) return;

	vr::InputAnalogActionData_t data = {};
	if (input->GetAnalogActionData(s_thumbstickAction, &data, sizeof(data), HandSource(hand)) != vr::VRInputError_None) {
		return;
	}
	if (!data.bActive) return;
	outX = data.x;
	outY = data.y;
}

bool QOverlay::VR::Input::ClickActive(Hand hand) {
	const int i = (hand == Hand::Left) ? 0 : 1;
	static SchmittTrigger click[2];
	float value = 0.0f;
	if (AnalogValue(s_triggerValueAction, hand, value)) {
		static bool logged[2] = {false, false};
		if (!logged[i]) { logged[i] = true; fmt::print("Click[{}]: analog trigger bound (threshold={:.2f})\n", i ? "R" : "L", Config::Instance().ClickThreshold()); }
		return click[i].Update(value, Config::Instance().ClickThreshold());
	}
	static bool loggedDigital[2] = {false, false};
	if (!loggedDigital[i]) { loggedDigital[i] = true; fmt::print("Click[{}]: analog trigger UNBOUND -> digital fallback (threshold ignored)\n", i ? "R" : "L"); }
	return DigitalActive(s_clickAction, hand);
}

bool QOverlay::VR::Input::GrabActive(Hand hand) {
	const int i = (hand == Hand::Left) ? 0 : 1;
	static SchmittTrigger grab[2];
	float value = 0.0f;
	if (AnalogValue(s_gripValueAction, hand, value)) {
		static bool logged[2] = {false, false};
		if (!logged[i]) { logged[i] = true; fmt::print("Grab[{}]: analog grip bound (threshold={:.2f})\n", i ? "R" : "L", Config::Instance().GrabThreshold()); }
		return grab[i].Update(value, Config::Instance().GrabThreshold());
	}
	static bool loggedDigital[2] = {false, false};
	if (!loggedDigital[i]) { loggedDigital[i] = true; fmt::print("Grab[{}]: analog grip UNBOUND -> digital fallback (threshold ignored)\n", i ? "R" : "L"); }
	return DigitalActive(s_grabAction, hand);
}

bool QOverlay::VR::Input::PointerPose(Hand hand, vr::HmdMatrix34_t& outPose) {
	auto input = vr::VRInput();
	if (input == nullptr || s_pointerPoseAction == vr::k_ulInvalidActionHandle) return false;

	vr::InputPoseActionData_t data = {};
	if (input->GetPoseActionDataForNextFrame(s_pointerPoseAction, vr::TrackingUniverseStanding, &data, sizeof(data), HandSource(hand)) != vr::VRInputError_None) {
		return false;
	}
	if (!data.bActive || !data.pose.bPoseIsValid) return false;

	outPose = data.pose.mDeviceToAbsoluteTracking;
	return true;
}

void QOverlay::VR::Input::TriggerHaptic(Hand hand, float durationSeconds, float frequency, float amplitude) {
	auto input = vr::VRInput();
	if (input == nullptr || s_hapticAction == vr::k_ulInvalidActionHandle) return;

	input->TriggerHapticVibrationAction(s_hapticAction, 0.0f, durationSeconds, frequency, amplitude, HandSource(hand));
}
