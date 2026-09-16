#pragma once

#include "vr/ioverlay_scene.h"

#include <memory>

#include <QObject>
#include <QPointF>
#include <QSize>
#include <QUrl>

#include <glm/vec2.hpp>
#include <openvr.h>

#include <QtGui/qopengl.h>

class QOpenGLContext;
class QOffscreenSurface;
class QQuickRenderControl;
class QQuickWindow;
class QQmlEngine;
class QQmlContext;
class QQmlComponent;
class QQuickItem;

namespace QOverlay::VR {

// Exposed to QML as the `pointer` context property so the .qml can draw its own
// VR cursor (position + press state) instead of us compositing one in C++.
class PointerState : public QObject {
	Q_OBJECT
	Q_PROPERTY(qreal x READ x NOTIFY changed)
	Q_PROPERTY(qreal y READ y NOTIFY changed)
	Q_PROPERTY(bool active READ active NOTIFY changed)
	Q_PROPERTY(bool pressed READ pressed NOTIFY changed)
public:
	using QObject::QObject;

	qreal x() const { return m_pos.x(); }
	qreal y() const { return m_pos.y(); }
	bool active() const { return m_active; }
	bool pressed() const { return m_pressed; }

	void Update(const QPointF& pos, bool active, bool pressed);

signals:
	void changed();
	void pressedDown(); // emitted on a not-pressed -> pressed transition (a fresh click)

private:
	QPointF m_pos;
	bool m_active = false;
	bool m_pressed = false;
};

// QQuickRenderControl-based alternative to OverlayScene: renders a QML scene into
// an offscreen FBO and submits its GL texture to the OpenVR overlay. Kept as a
// separate class so it sits side-by-side with the QGraphicsScene-based path.
class QmlOverlayScene : public QObject, public IOverlayScene {
	Q_OBJECT
	Q_DISABLE_COPY(QmlOverlayScene)

public:
	QmlOverlayScene(QObject* parent = nullptr);
	~QmlOverlayScene() override;

	bool Ok() const override { return m_quickWindow != nullptr; }
	bool Ready() const override { return Ok() && m_handle != vr::k_ulOverlayHandleInvalid && m_textures[0] != 0 && m_rootItem != nullptr; }

	vr::VROverlayHandle_t Handle() const override { return m_handle; }
	QSize Size() const override { return m_size; }

	// VR raycast -> pointer, per hand (0=left, 1=right). NoButton means hover-only. Updates
	// that hand's PointerState (for multi-cursor / two-hand key input) and drives the single
	// QMouseEvent stream used by MouseArea-based QML.
	bool FireMouseEvent(int hand, Qt::MouseButton button, const glm::vec2& pos) override;
	void MouseNotPresent(int hand) override;
	void FireScroll(int hand, const glm::vec2& pos, int notches) override;

	// QML overlays draw no cursor of their own now — the raycaster floats a per-hand cursor
	// overlay over them instead (so pointer motion doesn't re-rasterize the scene).
	bool ShowsVrCursor() const override { return true; }

public slots:
	void SetHandle(vr::VROverlayHandle_t handle) override;
	// Load a .qml file as overlay content.
	void SetSource(const QUrl& source);

	// Expose a C++ object to the QML scene as a context property. Call before
	// SetSource so the loaded component can bind to it.
	void SetContextProperty(const QString& name, QObject* object);

signals:
	void HandleChanged(vr::VROverlayHandle_t handle);
	void SizeChanged(const QSize& size);
	void SourceChanged(const QUrl& source);

private:
	void resize(const QSize& size);
	void renderVR();      // render QML -> FBO -> submit to OpenVR
	void requestRender(); // schedule a render on the event loop

	vr::VROverlayHandle_t m_handle = vr::k_ulOverlayHandleInvalid;

	// These are QObject children of `this` (parent ownership is a real backstop) and are
	// deleted explicitly, in order, in the destructor while the GL context is current.
	QOpenGLContext* m_glctx = nullptr;
	QOffscreenSurface* m_surface = nullptr;
	QQuickRenderControl* m_renderControl = nullptr;
	// The QQmlEngine is shared process-wide across all overlays (see SharedQmlEngine) so N
	// overlays don't each pay for their own JS heap + type caches. This scene owns only a child
	// QQmlContext off that engine, holding its own context properties (pointers, app,
	// windowManager, ctl) so overlays stay isolated from each other.
	QQmlContext* m_context = nullptr;
	QQmlComponent* m_component = nullptr;
	PointerState* m_pointers[2] = { nullptr, nullptr }; // 0 = left hand, 1 = right hand

	// Owned outright, NOT via a QObject parent: a QQuickWindow is a top-level window (no
	// parent), and the root item created by QQmlComponent::create() is caller-owned
	// (setParentItem sets only the visual parent). unique_ptr makes that ownership explicit
	// and fixes a leak where re-loading a source dropped the previous root item.
	std::unique_ptr<QQuickWindow> m_quickWindow;
	std::unique_ptr<QQuickItem> m_rootItem;

	// A 2-texture ring that QQuickRenderControl (QRhi) renders into and that we submit to
	// OpenVR. Two buffers remove the write-after-read hazard: frame N renders into
	// m_textures[i] while OpenVR may still be reading m_textures[1-i] from frame N-1, so our
	// render never has to wait on OpenVR's async read. Created manually rather than via
	// QOpenGLFramebufferObject — letting QRhi import a bare texture avoids the double-FBO
	// conflict that left a GL_INVALID_OPERATION and got the texture rejected as
	// VROverlayError_InvalidTexture.
	GLuint m_textures[2] = { 0, 0 };
	// Per-buffer GL fence (GLsync, stored as void* to keep GL sync types out of the header):
	// signalled when that buffer's render finishes. Waited on before OpenVR reads (so it
	// never samples a half-drawn frame) and before the buffer is reused. Replaces glFinish() —
	// waits only for OUR render commands, not a full GPU pipeline drain.
	void* m_fences[2] = { nullptr, nullptr };
	int m_frameIndex = 0; // which buffer the NEXT frame renders into (0/1)
	vr::Texture_t m_tex = {};
	QSize m_size;

	bool m_renderScheduled = false;
	bool m_diagLogged = false;       // one-shot texture diagnostics
	std::uint64_t m_submitErrors = 0; // rate-limit SetOverlayTexture error spam
	Qt::MouseButton m_lastButton = Qt::NoButton;
	QPointF m_lastPos;
	bool m_present = false;
	int m_mouseOwner = -1; // which hand drives the single QMouseEvent stream (-1 = none)
};
}
