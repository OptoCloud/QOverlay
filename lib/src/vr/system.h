#pragma once

#include <openvr.h>
#include <string>

namespace QOverlay::VR {
class Overlay;

// Identifies the consuming app to SteamVR: its app key (must be unique - shows up in the
// controller-binding UI and SteamVR's per-app settings), display name, and where its action
// manifest lives (relative to the executable's own directory). Defaults match this library's
// own QOverlay app; every other consumer should pass its own identity so it doesn't register
// itself as "QOverlay" and doesn't need a bindings/qoverlay_actions.json of its own.
struct AppIdentity {
	std::string appKey = "qoverlay.overlay";
	std::string appName = "QOverlay";
	std::string actionsRelativePath = "bindings/qoverlay_actions.json";
};

struct VRSystem {
	static bool Initialize(const AppIdentity& identity = {});
	static void Shutdown();
	static bool IsInitialized();

	static void Update() {
		PollInput();
	}
protected:
	friend class Overlay;
	static void RegisterOverlay(Overlay* overlay);
	static void UnregisterOverlay(Overlay* overlay);
private:
	static void PollInput();
};
}
