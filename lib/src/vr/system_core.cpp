#include "vr/system.h"

#include "vr/input.h"
#include "log.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>

#include <fmt/core.h>
#include <openvr.h>

vr::IVRSystem* s_vrSystem;

// App key must match the one declared in qoverlay.vrmanifest.
static constexpr const char* kAppKey = "qoverlay.overlay";

// Registers the application manifest with SteamVR and identifies this running
// process against it. This gives the app a stable app key so its actions show up
// (with a persistent, per-app binding set) in the SteamVR controller binding UI.
// Best-effort: a failure here is non-fatal, bindings still work via the action
// manifest loaded in Input::Initialize.
// Backslash-escape a Windows path so it can be embedded in a JSON string literal.
static std::string JsonPath(const QString& path) {
	QString native = QDir::toNativeSeparators(path);
	native.replace("\\", "\\\\");
	return native.toStdString();
}

static void RegisterApplicationManifest() {
	auto apps = vr::VRApplications();
	if (apps == nullptr) {
		LOG_ERROR("Failed to get IVRApplications interface");
		return;
	}

	// Generate the manifest at runtime with ABSOLUTE paths. SteamVR resolves a manifest's
	// relative binary_path against the working directory (not the manifest's directory);
	// when launched from a debugger/CLI the CWD isn't the exe dir, so the static
	// manifest's "QOverlay.exe" didn't resolve and the app never registered
	// (AddApplicationManifest returned success but IsApplicationInstalled stayed false and
	// IdentifyApplication failed with UnknownApplication). An absolute binary_path that
	// matches the running process image fixes both.
	const QString appDir = QCoreApplication::applicationDirPath();
	const std::string exePath = JsonPath(QCoreApplication::applicationFilePath());
	const std::string actionsPath = JsonPath(QDir(appDir).filePath("bindings/qoverlay_actions.json"));

	const std::string json = fmt::format(
		"{{\n"
		"  \"applications\": [\n"
		"    {{\n"
		"      \"app_key\": \"{}\",\n"
		"      \"launch_type\": \"binary\",\n"
		"      \"binary_path_windows\": \"{}\",\n"
		"      \"is_dashboard_overlay\": false,\n"
		"      \"action_manifest_path\": \"{}\",\n"
		"      \"strings\": {{ \"en_US\": {{ \"name\": \"QOverlay\", \"description\": \"Open source overlay application\" }} }}\n"
		"    }}\n"
		"  ]\n"
		"}}\n",
		kAppKey, exePath, actionsPath);

	const QString genPath = QDir(appDir).filePath("qoverlay.generated.vrmanifest");
	{
		QFile file(genPath);
		if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
			LOG_ERROR("Failed to write generated manifest to {}", genPath.toStdString());
			return;
		}
		file.write(QByteArray::fromStdString(json));
	}

	// Session-temporary registration (not persisted into SteamVR's app config).
	if (const vr::EVRApplicationError error = apps->AddApplicationManifest(genPath.toUtf8().constData(), true); error != vr::VRApplicationError_None) {
		LOG_WARN("Failed to add application manifest: {}", apps->GetApplicationsErrorNameFromEnum(error));
	}

	if (!apps->IsApplicationInstalled(kAppKey)) {
		LOG_WARN("'{}' not reported installed after AddApplicationManifest", kAppKey);
	}

	if (const vr::EVRApplicationError error = apps->IdentifyApplication(static_cast<std::uint32_t>(QCoreApplication::applicationPid()), kAppKey); error != vr::VRApplicationError_None) {
		LOG_WARN("Failed to identify application: {}", apps->GetApplicationsErrorNameFromEnum(error));
	} else {
		LOG_INFO("Identified application as '{}'", kAppKey);
	}
}

bool QOverlay::VR::VRSystem::Initialize() {
	vr::EVRInitError error;
	s_vrSystem = vr::VR_Init(&error, vr::VRApplication_Overlay);
	if (error != vr::VRInitError_None) {
		LOG_ERROR("Failed to initialize OpenVR: {}", vr::VR_GetVRInitErrorAsEnglishDescription(error));
		return false;
	}

	RegisterApplicationManifest();

	if (!Input::Initialize()) {
		// Non-fatal: the overlay still renders, but controller interaction/haptics
		// won't work until the action manifest loads correctly.
		LOG_WARN("VR input initialization failed; controller interaction disabled");
	}

	return true;
}

void QOverlay::VR::VRSystem::Shutdown() {
	s_vrSystem = nullptr;
	vr::VR_Shutdown();
}

bool QOverlay::VR::VRSystem::IsInitialized() {
	return s_vrSystem != nullptr;
}
