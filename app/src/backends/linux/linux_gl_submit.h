#pragma once

#include <cstdint>

#include <QSize>

#include <openvr.h>

class QOpenGLContext;
class QOffscreenSurface;

namespace QOverlay::Capture::Linux {

// Shared GL frame → OpenVR submitter for the Linux backends. OpenVR on Linux takes an
// OpenGL texture (TextureType_OpenGL), so each capture source owns one of these: it keeps
// a private QOpenGLContext + offscreen surface and a GL texture, and submits that texture
// to the overlay. Two upload paths:
//   - UploadRGBA(): CPU pixel upload (X11 XShm/XImage path).
//   - AdoptTexture(): submit an already-GPU texture id (dmabuf→EGLImage→GL path for the
//     PipeWire/wlroots backends), no copy.
//
// NOTE: Untested — written on Windows; needs bring-up on Linux (see docs/LINUX_BACKENDS.md).
class GlSubmitter {
public:
	GlSubmitter();
	~GlSubmitter();

	GlSubmitter(const GlSubmitter&) = delete;
	GlSubmitter& operator=(const GlSubmitter&) = delete;

	bool ensureContext();

	// Upload a tightly-packed RGBA/BGRA buffer into the managed texture (creates/resizes
	// as needed), then submit it to `overlay`.
	void UploadAndSubmit(vr::VROverlayHandle_t overlay, const void* pixels, QSize size, bool bgra);

	// Submit an externally-owned GL texture (e.g. imported from a dmabuf) directly.
	void SubmitTexture(vr::VROverlayHandle_t overlay, unsigned int glTexture, QSize size);

	QSize size() const { return m_size; }

private:
	void submitHandle(vr::VROverlayHandle_t overlay, unsigned int glTexture);

	QOpenGLContext* m_ctx = nullptr;
	QOffscreenSurface* m_surface = nullptr;
	unsigned int m_texture = 0; // GL texture owned by us (UploadAndSubmit path)
	QSize m_size;
};

}
