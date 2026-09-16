#pragma once

#include "backends/windows/surface_enum.h"

#include <QImage>

namespace QOverlay::Capture::Win {

// Grab a one-shot preview of a capture target, scaled so its longest side is `maxDim`.
// PrintWindow(PW_RENDERFULLCONTENT) for windows (works for D3D/UWP/Chrome), a screen
// BitBlt for monitors. Returns a null QImage on failure.
QImage GrabThumbnail(const CaptureTarget& target, int maxDim);

}
