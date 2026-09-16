#include "vr/qml_overlay_scene.h"

#include "log.h"

#include <QCoreApplication>
#include <QMouseEvent>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLExtraFunctions>
#include <QOpenGLFunctions>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickGraphicsDevice>
#include <QQuickItem>
#include <QQuickRenderControl>
#include <QQuickRenderTarget>
#include <QQuickWindow>
#include <QSurfaceFormat>

#include <fmt/core.h>

#ifndef GL_RGBA8
#define GL_RGBA8 0x8058
#endif

// GL 3.2 sync-object constants (glFenceSync/glClientWaitSync), in case the platform GL
// headers Qt pulls in don't define them.
#ifndef GL_SYNC_GPU_COMMANDS_COMPLETE
#define GL_SYNC_GPU_COMMANDS_COMPLETE 0x9117
#endif
#ifndef GL_SYNC_FLUSH_COMMANDS_BIT
#define GL_SYNC_FLUSH_COMMANDS_BIT 0x00000001
#endif

namespace {
// Upper bound on how long we'll block waiting for our own render to finish before OpenVR
// reads the texture. The render always completes far sooner; this is only a safety cap so a
// lost fence can't hang the VR loop. glClientWaitSync's timeout is in nanoseconds.
constexpr GLuint64 kFenceTimeoutNs = 1'000'000'000ull; // 1 s

// One QQmlEngine shared by every QmlOverlayScene. A QQmlEngine is heavy (its own V4 JS heap,
// compiled-type cache and import database), and the app spawns a QML overlay per panel *and*
// per capture-overlay control bar — so one engine per overlay made memory climb with overlay
// count. Each scene instead loads its component into its own child QQmlContext of this engine,
// which keeps per-overlay context properties isolated while sharing the engine's caches.
// Created lazily on the first scene (so the QGuiApplication already exists) and intentionally
// never destroyed — tearing a QQmlEngine down during static destruction, after the app is
// gone, is unsafe; the OS reclaims it at process exit.
QQmlEngine* SharedQmlEngine() {
	static QQmlEngine* engine = new QQmlEngine();
	return engine;
}
}

void QOverlay::VR::PointerState::Update(const QPointF& pos, bool active, bool pressed) {
	if (m_pos == pos && m_active == active && m_pressed == pressed) return;
	const bool rising = pressed && !m_pressed;
	m_pos = pos;
	m_active = active;
	m_pressed = pressed;
	emit changed();
	if (rising) emit pressedDown();
}

QOverlay::VR::QmlOverlayScene::QmlOverlayScene(QObject* parent)
	: QObject(parent)
{
	// The Qt Quick scene graph must run on the OpenGL RHI backend so the frame it
	// produces is a GL texture we can hand to OpenVR (TextureType_OpenGL). This is
	// process-global and must happen before the first QQuickWindow is created; for
	// the real app it belongs in main() before the QApplication is exec'd.
	static bool graphicsApiSet = false;
	if (!graphicsApiSet) {
		QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);
		graphicsApiSet = true;
	}

	QSurfaceFormat format;
	format.setDepthBufferSize(24);
	format.setStencilBufferSize(8);
	format.setVersion(3, 3);
	format.setProfile(QSurfaceFormat::CoreProfile);
	QSurfaceFormat::setDefaultFormat(format);

	m_glctx = new QOpenGLContext(this);
	m_glctx->setFormat(format);
	if (!m_glctx->create()) {
		LOG_ERROR("QmlOverlayScene: failed to create OpenGL context");
		return;
	}

	m_surface = new QOffscreenSurface(nullptr, this);
	m_surface->setFormat(m_glctx->format());
	m_surface->create();

	m_renderControl = new QQuickRenderControl(this);
	m_quickWindow = std::make_unique<QQuickWindow>(m_renderControl);
	// Transparent background so the QML itself defines the overlay's shape/chrome
	// (rounded corners, drop shadows, etc. — a QWidget/QPainter overlay can't do this cheaply).
	m_quickWindow->setColor(Qt::transparent);

	// Load this overlay's QML into its own child context of the shared engine, so its context
	// properties (pointers, and whatever SetContextProperty adds) don't leak into other
	// overlays. No incubation controller is set: components are created synchronously, and
	// async image loading uses its own loader thread, not the engine's incubator.
	m_context = new QQmlContext(SharedQmlEngine()->rootContext(), this);

	// Expose a per-hand VR pointer to QML so the scene can draw two cursors and let both
	// controllers interact at once (e.g. two-hand typing). `pointer` aliases the left one
	// for any single-cursor QML that still references it.
	m_pointers[0] = new PointerState(this);
	m_pointers[1] = new PointerState(this);
	m_context->setContextProperty("pointerLeft", m_pointers[0]);
	m_context->setContextProperty("pointerRight", m_pointers[1]);
	m_context->setContextProperty("pointer", m_pointers[0]);

	// Make QRhi adopt OUR QOpenGLContext instead of creating its own. Without this
	// the render control renders into a different context, so the texture we submit
	// to OpenVR is never drawn into (QRhi raised GL_INVALID_OPERATION and OpenVR
	// rejected it as VROverlayError_InvalidTexture). Must precede initialize().
	m_glctx->makeCurrent(m_surface);
	m_quickWindow->setGraphicsDevice(QQuickGraphicsDevice::fromOpenGLContext(m_glctx));
	if (!m_renderControl->initialize()) {
		LOG_ERROR("QmlOverlayScene: failed to initialize QQuickRenderControl");
	}
	m_glctx->doneCurrent();

	connect(m_renderControl, &QQuickRenderControl::renderRequested, this, &QmlOverlayScene::requestRender);
	connect(m_renderControl, &QQuickRenderControl::sceneChanged, this, &QmlOverlayScene::requestRender);
}

QOverlay::VR::QmlOverlayScene::~QmlOverlayScene() {
	// The QQuickWindow / render control hold RHI (GL) resources that must be released with
	// OUR GL context current. These objects are parented to this scene, so if we let the
	// QObject destructor delete them (after this body has already called doneCurrent), the
	// RHI teardown runs with no current context and crashes. So delete them EXPLICITLY here,
	// in dependency order, while the context is current (deleting a child also removes it
	// from our child list, so QObject won't double-delete).
	if (m_glctx != nullptr && m_surface != nullptr) {
		m_glctx->makeCurrent(m_surface);
	}
	if (m_renderControl != nullptr) {
		m_renderControl->invalidate();
	}

	// Reclaim any outstanding GL fences while the context is current (before the RHI/context
	// teardown below), so no sync object leaks.
	if (m_glctx != nullptr) {
		auto* ef = m_glctx->extraFunctions();
		for (void*& fence : m_fences) {
			if (fence != nullptr) {
				ef->glDeleteSync(static_cast<GLsync>(fence));
				fence = nullptr;
			}
		}
	}

	delete m_component;     m_component = nullptr;
	m_rootItem.reset();                                // our item — delete before its host window
	m_quickWindow.reset();                             // tears down the scene graph / RHI
	delete m_renderControl; m_renderControl = nullptr;
	delete m_context;       m_context = nullptr;       // our child context; shared engine stays
	// (SharedQmlEngine() is process-wide and intentionally not destroyed here.)

	if (m_glctx != nullptr) {
		auto* f = m_glctx->functions();
		for (GLuint& tex : m_textures) {
			if (tex != 0) {
				f->glDeleteTextures(1, &tex);
				tex = 0;
			}
		}
	}
	if (m_glctx != nullptr) {
		m_glctx->doneCurrent();
	}
}

void QOverlay::VR::QmlOverlayScene::SetHandle(vr::VROverlayHandle_t handle) {
	if (!Ok() || m_handle == handle) return;

	m_handle = handle;
	emit HandleChanged(m_handle);
	requestRender();
}

void QOverlay::VR::QmlOverlayScene::SetContextProperty(const QString& name, QObject* object) {
	if (m_context == nullptr) return;
	m_context->setContextProperty(name, object);
}

void QOverlay::VR::QmlOverlayScene::SetSource(const QUrl& source) {
	if (!Ok()) return;

	delete m_component;
	m_component = new QQmlComponent(SharedQmlEngine(), source, this);
	if (m_component->isError()) {
		for (const QQmlError& error : m_component->errors()) {
			LOG_ERROR("QmlOverlayScene: QML error loading {}: {}", source.toString().toStdString(), error.toString().toStdString());
		}
		return;
	}

	QObject* created = m_component->create(m_context);
	auto* item = qobject_cast<QQuickItem*>(created);
	if (item == nullptr) {
		LOG_ERROR("QmlOverlayScene: root of {} is not a QQuickItem", source.toString().toStdString());
		delete created;
		return;
	}

	// reset() destroys any previously-loaded root item (fixes the leak on re-SetSource — the
	// old item is owned here, not by the window's content item, so nothing else frees it).
	m_rootItem.reset(item);
	m_rootItem->setParentItem(m_quickWindow->contentItem());

	// Prefer the QML's implicit size; fall back to a sane default.
	QSize size(static_cast<int>(m_rootItem->width()), static_cast<int>(m_rootItem->height()));
	if (size.isEmpty()) {
		const qreal iw = m_rootItem->implicitWidth();
		const qreal ih = m_rootItem->implicitHeight();
		size = (iw > 0 && ih > 0) ? QSize(static_cast<int>(iw), static_cast<int>(ih)) : QSize(512, 512);
	}

	resize(size);
	emit SourceChanged(source);
	requestRender();
}

void QOverlay::VR::QmlOverlayScene::resize(const QSize& size) {
	if (!Ok() || size.isEmpty() || size == m_size) return;

	m_size = size;

	m_quickWindow->setGeometry(0, 0, size.width(), size.height());
	if (m_rootItem != nullptr) {
		m_rootItem->setWidth(size.width());
		m_rootItem->setHeight(size.height());
	}

	m_glctx->makeCurrent(m_surface);
	auto* f = m_glctx->functions();
	auto* ef = m_glctx->extraFunctions();

	// Recreate both ring textures at the new size. QRhi imports these bare textures and
	// manages its own depth/stencil + framebuffer around each (Qt 6 rendercontrol pattern).
	// Any fence tied to the old textures is meaningless now — drop it. The render target is
	// bound per-frame in renderVR() (it alternates between the two textures), not here.
	for (int i = 0; i < 2; ++i) {
		if (m_fences[i] != nullptr) {
			ef->glDeleteSync(static_cast<GLsync>(m_fences[i]));
			m_fences[i] = nullptr;
		}
		if (m_textures[i] != 0) {
			f->glDeleteTextures(1, &m_textures[i]);
			m_textures[i] = 0;
		}
		f->glGenTextures(1, &m_textures[i]);
		f->glBindTexture(GL_TEXTURE_2D, m_textures[i]);
		f->glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, size.width(), size.height(), 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
		f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	}
	f->glBindTexture(GL_TEXTURE_2D, 0);
	m_frameIndex = 0;

	m_glctx->doneCurrent();

	m_tex.eType = vr::TextureType_OpenGL;
	m_tex.eColorSpace = vr::ColorSpace_Auto;

	emit SizeChanged(m_size);
}

void QOverlay::VR::QmlOverlayScene::requestRender() {
	if (m_renderScheduled || !Ready()) return;
	m_renderScheduled = true;
	QMetaObject::invokeMethod(this, [this]() {
		m_renderScheduled = false;
		renderVR();
	}, Qt::QueuedConnection);
}

void QOverlay::VR::QmlOverlayScene::renderVR() {
	if (!Ready()) return;

	auto overlay = vr::VROverlay();
	if (overlay == nullptr) return;

	m_glctx->makeCurrent(m_surface);
	auto* f = m_glctx->functions();
	auto* ef = m_glctx->extraFunctions();

	const GLenum errBefore = f->glGetError(); // pending error from setup/previous frame

	const int i = m_frameIndex;

	// Reclaim this buffer's previous fence before overwriting it. With two buffers this fence
	// is from two frames ago and has long completed, so the wait returns immediately — it just
	// guarantees the GPU is done with the buffer we're about to render into.
	if (m_fences[i] != nullptr) {
		ef->glClientWaitSync(static_cast<GLsync>(m_fences[i]), GL_SYNC_FLUSH_COMMANDS_BIT, kFenceTimeoutNs);
		ef->glDeleteSync(static_cast<GLsync>(m_fences[i]));
		m_fences[i] = nullptr;
	}

	// Render frame N into m_textures[i] while OpenVR may still be reading m_textures[1-i].
	m_quickWindow->setRenderTarget(QQuickRenderTarget::fromOpenGLTexture(m_textures[i], m_size));

	m_renderControl->polishItems();
	m_renderControl->beginFrame();
	m_renderControl->sync();
	m_renderControl->render();
	m_renderControl->endFrame();

	// Fence + wait instead of glFinish(): block only until OUR render commands complete (so
	// OpenVR reads a finished texture on its own context), not until the entire GPU pipeline
	// drains. GL_SYNC_FLUSH_COMMANDS_BIT flushes the fence so it's guaranteed to be reached.
	GLsync fence = ef->glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
	ef->glClientWaitSync(fence, GL_SYNC_FLUSH_COMMANDS_BIT, kFenceTimeoutNs);

	// One-shot diagnostics: with QRhi on our context, errAfterRender should be 0x0000.
	if (!m_diagLogged) {
		m_diagLogged = true;
		const GLenum errAfterRender = f->glGetError();
		fmt::print("QmlOverlayScene: submit diag — texId={} size={}x{} errBefore=0x{:04x} errAfterRender=0x{:04x} ctxValid={}\n",
			m_textures[i], m_size.width(), m_size.height(),
			static_cast<unsigned>(errBefore), static_cast<unsigned>(errAfterRender), m_glctx->isValid());
	}

	m_tex.handle = reinterpret_cast<void*>(static_cast<quintptr>(m_textures[i]));
	const vr::EVROverlayError error = overlay->SetOverlayTexture(m_handle, &m_tex);

	// Keep the fence with the buffer; reclaimed above the next time this buffer is reused.
	m_fences[i] = fence;
	m_frameIndex ^= 1;

	m_glctx->doneCurrent();

	if (error != vr::VROverlayError_None) {
		// Rate-limit: log the first failure, then every 100th, so the console stays usable.
		if (m_submitErrors == 0 || (m_submitErrors % 100) == 0) {
			fmt::print("QmlOverlayScene: SetOverlayTexture failed ({}x): {}\n",
				m_submitErrors + 1, overlay->GetOverlayErrorNameFromEnum(error));
		}
		++m_submitErrors;
	} else if (m_submitErrors > 0) {
		fmt::print("QmlOverlayScene: SetOverlayTexture recovered after {} failures\n", m_submitErrors);
		m_submitErrors = 0;
	}

	// NOTE: The RHI renders top-left origin while OpenVR samples GL textures
	// bottom-left; if the overlay appears vertically flipped on a headset, flip V
	// via IVROverlay::SetOverlayTextureBounds (vMin=1, vMax=0).
}

bool QOverlay::VR::QmlOverlayScene::FireMouseEvent(int hand, Qt::MouseButton button, const glm::vec2& pos) {
	if (!Ok()) return false;

	const QPointF local(pos.x, pos.y);

	// Per-hand pointer: drives the two cursors and lets QML react to each ray independently
	// (hover highlight + two-hand key presses via PointerState::pressedDown).
	if (hand >= 0 && hand < 2 && m_pointers[hand] != nullptr) {
		m_pointers[hand]->Update(local, true, button != Qt::NoButton);
	}

	// Single QMouseEvent stream for MouseArea-based QML (the panel/launcher/control bars).
	// Only ONE hand drives it at a time — the first to press owns it until release — so the
	// other hand's hover can't synthesize a spurious release that cancels the owner's click.
	// The keyboard keys don't use MouseAreas (they use the per-hand pointers above).
	if (button != Qt::NoButton && m_mouseOwner == -1) m_mouseOwner = hand;
	if (m_mouseOwner != -1 && m_mouseOwner != hand) {
		// This hand doesn't own the mouse stream. Its PointerState was already updated above;
		// any resulting visual change (keyboard hot-highlight, press activation) dirties the
		// scene and re-renders via QQuickRenderControl::sceneChanged. No render forced here —
		// the moving cursor is a separate overlay now, not a QML item.
		return true;
	}
	if (button == Qt::NoButton && m_mouseOwner == hand) m_mouseOwner = -1; // owner released

	QEvent::Type type;
	if (button != Qt::NoButton && m_lastButton == Qt::NoButton) {
		type = QEvent::MouseButtonPress;
	} else if (button == Qt::NoButton && m_lastButton != Qt::NoButton) {
		type = QEvent::MouseButtonRelease;
	} else {
		type = QEvent::MouseMove;
	}

	const Qt::MouseButton eventButton = (type != QEvent::MouseMove)
		? (button != Qt::NoButton ? button : m_lastButton)
		: Qt::NoButton;
	const Qt::MouseButtons buttons = (button != Qt::NoButton) ? Qt::MouseButtons(button) : Qt::NoButton;

	QMouseEvent event(type, local, local, eventButton, buttons, Qt::NoModifier);
	QCoreApplication::sendEvent(m_quickWindow.get(), &event);

	m_lastButton = button;
	m_lastPos = local;
	m_present = true;

	// Don't force a render on pointer MOVES — a pure move changes nothing visible now that the
	// cursor is its own overlay, so we rely on QQuickRenderControl::sceneChanged to render only
	// when hover-highlight or a bound property actually changes. Keep an explicit render on the
	// low-frequency press/release transitions as a guaranteed backstop for that visual change.
	if (type != QEvent::MouseMove) requestRender();
	return true;
}

void QOverlay::VR::QmlOverlayScene::MouseNotPresent(int hand) {
	// Deactivate this hand's cursor.
	if (hand >= 0 && hand < 2 && m_pointers[hand] != nullptr) {
		m_pointers[hand]->Update(QPointF(m_pointers[hand]->x(), m_pointers[hand]->y()), false, false);
	}

	// Only the hand that owns the mouse stream (or nobody) may end it — otherwise the other
	// hand leaving would release the owner's held button.
	if (m_mouseOwner != -1 && m_mouseOwner != hand) return;
	m_mouseOwner = -1;

	if (!m_present) return;

	if (m_lastButton != Qt::NoButton && m_quickWindow != nullptr) {
		QMouseEvent release(QEvent::MouseButtonRelease, m_lastPos, m_lastPos, m_lastButton, Qt::NoButton, Qt::NoModifier);
		QCoreApplication::sendEvent(m_quickWindow.get(), &release);
	}

	m_present = false;
	m_lastButton = Qt::NoButton;

	requestRender();
}
