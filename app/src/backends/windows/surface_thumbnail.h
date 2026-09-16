#pragma once

#include "backends/windows/surface_enum.h"

#include <QImage>

namespace QOverlay::Capture::Win {

// Grab a one-shot preview of a capture target, scaled so its longest side is `maxDim`.
// PrintWindow(PW_RENDERFULLCONTENT) for windows (works for D3D/UWP/Chrome), a screen
// BitBlt for monitors. Returns a null QImage on failure.
QImage GrabThumbnail(const CaptureTarget& target, int maxDim);

// Grab the window's application icon as a premultiplied ARGB QImage, at most `maxDim` px.
// Null for monitors or if the window exposes no icon.
QImage GrabIcon(const CaptureTarget& target, int maxDim);

}
