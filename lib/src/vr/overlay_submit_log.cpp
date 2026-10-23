#include "vr/overlay_submit_log.h"

#include "log.h"

void QOverlay::VR::LogSubmitResult(std::string_view label, vr::IVROverlay* overlay,
	vr::EVROverlayError error, std::uint64_t& errorCount) {
	if (error != vr::VROverlayError_None) {
		if (errorCount == 0 || (errorCount % 100) == 0) {
			LOG_ERROR("{}: SetOverlayTexture failed ({}x): {}", label, errorCount + 1,
				overlay != nullptr ? overlay->GetOverlayErrorNameFromEnum(error) : "no overlay interface");
		}
		++errorCount;
	} else if (errorCount > 0) {
		LOG_INFO("{}: SetOverlayTexture recovered after {} failures", label, errorCount);
		errorCount = 0;
	}
}
