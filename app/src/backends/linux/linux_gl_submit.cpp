#include "backends/linux/linux_gl_submit.h"

#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QSurfaceFormat>

#include <fmt/core.h>

#ifndef GL_BGRA
#define GL_BGRA 0x80E1
#endif
#ifndef GL_RGBA8
#define GL_RGBA8 0x8058
#endif

using QOverlay::Capture::Linux::GlSubmitter;

GlSubmitter::GlSubmitter() = default;

GlSubmitter::~GlSubmitter() {
	if (m_ctx != nullptr && m_surface != nullptr) {
		m_ctx->makeCurrent(m_surface);
		if (m_texture != 0) {
			m_ctx->functions()->glDeleteTextures(1, &m_texture);
			m_texture = 0;
		}
		m_ctx->doneCurrent();
	}
	delete m_ctx;
	delete m_surface;
}

bool GlSubmitter::ensureContext() {
	if (m_ctx != nullptr) return true;

	QSurfaceFormat format;
	format.setRenderableType(QSurfaceFormat::OpenGL);
	format.setVersion(3, 3);
	format.setProfile(QSurfaceFormat::CoreProfile);

	m_ctx = new QOpenGLContext();
	m_ctx->setFormat(format);
	if (!m_ctx->create()) {
		fmt::print("GlSubmitter: failed to create GL context\n");
		delete m_ctx;
		m_ctx = nullptr;
		return false;
	}

	m_surface = new QOffscreenSurface();
	m_surface->setFormat(m_ctx->format());
	m_surface->create();
	return true;
}

void GlSubmitter::UploadAndSubmit(vr::VROverlayHandle_t overlay, const void* pixels, QSize size, bool bgra) {
	if (!ensureContext() || pixels == nullptr || size.isEmpty()) return;

	m_ctx->makeCurrent(m_surface);
	auto* f = m_ctx->functions();

	if (m_texture == 0 || size != m_size) {
		if (m_texture != 0) f->glDeleteTextures(1, &m_texture);
		f->glGenTextures(1, &m_texture);
		f->glBindTexture(GL_TEXTURE_2D, m_texture);
		f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		f->glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, size.width(), size.height(), 0,
			bgra ? GL_BGRA : GL_RGBA, GL_UNSIGNED_BYTE, pixels);
		m_size = size;
	} else {
		f->glBindTexture(GL_TEXTURE_2D, m_texture);
		f->glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, size.width(), size.height(),
			bgra ? GL_BGRA : GL_RGBA, GL_UNSIGNED_BYTE, pixels);
	}
	f->glBindTexture(GL_TEXTURE_2D, 0);
	f->glFinish();

	submitHandle(overlay, m_texture);
	m_ctx->doneCurrent();
}

void GlSubmitter::SubmitTexture(vr::VROverlayHandle_t overlay, unsigned int glTexture, QSize size) {
	if (!ensureContext() || glTexture == 0) return;
	m_size = size;
	m_ctx->makeCurrent(m_surface);
	submitHandle(overlay, glTexture);
	m_ctx->doneCurrent();
}

void GlSubmitter::submitHandle(vr::VROverlayHandle_t overlay, unsigned int glTexture) {
	auto ov = vr::VROverlay();
	if (ov == nullptr) return;

	vr::Texture_t tex{};
	tex.handle = reinterpret_cast<void*>(static_cast<uintptr_t>(glTexture));
	tex.eType = vr::TextureType_OpenGL;
	tex.eColorSpace = vr::ColorSpace_Auto;

	// OpenGL samples bottom-left origin; desktop frames are top-left, so callers that need
	// it should flip V via SetOverlayTextureBounds. Left to bring-up tuning.
	ov->SetOverlayTexture(overlay, &tex);
}
