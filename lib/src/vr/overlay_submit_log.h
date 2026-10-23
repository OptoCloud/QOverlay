#pragma once

#include <cstdint>
#include <string_view>

#include <openvr.h>

namespace QOverlay::VR {

// Rate-limited logging for repeated IVROverlay::SetOverlayTexture failures: logs the first
// failure and then every 100th while it keeps failing, then a single "recovered" message
// once it starts succeeding again, so a stuck headset/compositor doesn't spam the log every
// frame. `errorCount` is the caller's own consecutive-failure counter, updated in place.
void LogSubmitResult(std::string_view label, vr::IVROverlay* overlay, vr::EVROverlayError error,
	std::uint64_t& errorCount);

}
