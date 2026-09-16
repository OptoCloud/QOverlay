#pragma once

#include <algorithm>

namespace QOverlay::VR {

// A Schmitt trigger: a hysteresis gate for noisy analog inputs. It engages once the value
// reaches `press`, and only disengages once it falls below the release threshold, so a
// signal resting near the threshold can't rapidly toggle (chatter) into repeated clicks.
//
// The release threshold is a FRACTION of `press` (default 35% below) rather than a fixed
// absolute margin, so it scales with the threshold: a fixed margin equal to a small
// threshold (e.g. the 0.15 grab threshold) would drive the release point to 0 and the gate
// could never disengage — sticking the button on. One instance holds the state for one
// logical button; reuse it instead of duplicating the logic.
struct SchmittTrigger {
	bool engaged = false;

	bool Update(float value, float press, float releaseFraction = 0.35f) {
		const float release = press * (1.0f - releaseFraction);
		engaged = engaged ? (value >= release) : (value >= press);
		return engaged;
	}
};

}
