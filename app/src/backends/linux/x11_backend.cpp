#include "backends/linux/x11_backend.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>

#include <QImage>

#include <fmt/core.h>

// Xlib is included only in the .cpp so its macros don't leak into Qt headers.
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <X11/extensions/Xcomposite.h>
#include <X11/extensions/XTest.h>
#include <X11/extensions/Xrandr.h>
#include <X11/keysym.h>

namespace {

using QOverlay::Capture::CaptureSurface;
using QOverlay::Capture::Linux::X11Target;

QString HandleId(const char* prefix, unsigned long v) {
	return QString("%1:0x%2").arg(prefix).arg(v, 0, 16);
}

// Read a window's _NET_WM_NAME (UTF-8), falling back to WM_NAME.
QString WindowTitle(Display* dpy, Window win) {
	Atom netName = XInternAtom(dpy, "_NET_WM_NAME", False);
	Atom utf8 = XInternAtom(dpy, "UTF8_STRING", False);
	Atom type = 0; int fmt = 0; unsigned long n = 0, after = 0; unsigned char* data = nullptr;
	if (XGetWindowProperty(dpy, win, netName, 0, 1024, False, utf8, &type, &fmt, &n, &after, &data) == Success && data) {
		QString title = QString::fromUtf8(reinterpret_cast<char*>(data));
		XFree(data);
		if (!title.isEmpty()) return title;
	}
	char* wmName = nullptr;
	if (XFetchName(dpy, win, &wmName) && wmName) {
		QString title = QString::fromUtf8(wmName);
		XFree(wmName);
		return title;
	}
	return {};
}

// A window's WM_CLASS class name (e.g. "firefox", "Code") — the closest X11 analogue of the
// owning application, used to group windows by app in the Sources panel.
QString WindowClass(Display* dpy, Window win) {
	XClassHint hint{};
	if (!XGetClassHint(dpy, win, &hint)) return {};
	QString cls = hint.res_class ? QString::fromUtf8(hint.res_class) : QString();
	if (cls.isEmpty() && hint.res_name) cls = QString::fromUtf8(hint.res_name);
	if (hint.res_name) XFree(hint.res_name);
	if (hint.res_class) XFree(hint.res_class);
	return cls;
}

// A window's _NET_WM_ICON: a CARDINAL array of back-to-back [width, height, ARGB pixels…]
// entries, one per size. Picks the smallest entry at least `maxDim` on its longest side (or
// the largest available), scaled down to `maxDim`. Null image when the window has no icon.
QImage WindowIcon(Display* dpy, Window win, int maxDim) {
	Atom netIcon = XInternAtom(dpy, "_NET_WM_ICON", True);
	if (netIcon == None) return {};
	Atom type = 0; int fmt = 0; unsigned long n = 0, after = 0; unsigned char* data = nullptr;
	if (XGetWindowProperty(dpy, win, netIcon, 0, 1L << 24, False, XA_CARDINAL, &type, &fmt, &n, &after, &data) != Success || data == nullptr) return {};

	// Format-32 properties come back as an array of C `long`, not 32-bit ints (so 8 bytes per
	// element on LP64), with each pixel in the low 32 bits.
	const auto* v = reinterpret_cast<const unsigned long*>(data);
	unsigned long best = n, bestW = 0, bestH = 0;
	for (unsigned long i = 0; i + 2 <= n;) {
		const unsigned long w = v[i], h = v[i + 1];
		if (w == 0 || h == 0 || w > 4096 || h > 4096 || w * h > n - i - 2) break;
		const unsigned long dim = std::max(w, h), bestDim = std::max(bestW, bestH);
		const bool better = best == n
			|| (bestDim < static_cast<unsigned long>(maxDim) && dim > bestDim)
			|| (dim >= static_cast<unsigned long>(maxDim) && dim < bestDim);
		if (better) { best = i; bestW = w; bestH = h; }
		i += 2 + w * h;
	}

	QImage icon;
	if (best != n) {
		icon = QImage(static_cast<int>(bestW), static_cast<int>(bestH), QImage::Format_ARGB32);
		const unsigned long* px = v + best + 2;
		for (unsigned long y = 0; y < bestH; ++y) {
			auto* line = reinterpret_cast<QRgb*>(icon.scanLine(static_cast<int>(y)));
			for (unsigned long x = 0; x < bestW; ++x) line[x] = static_cast<QRgb>(px[y * bestW + x] & 0xffffffffUL);
		}
	}
	XFree(data);
	if (icon.isNull()) return {};
	if (icon.width() > maxDim || icon.height() > maxDim) icon = icon.scaled(maxDim, maxDim, Qt::KeepAspectRatio, Qt::SmoothTransformation);
	return icon;
}

// Convert an XImage (assumed 32-bit BGRA/TrueColor) to a QImage copy.
QImage ImageFromX(XImage* img) {
	if (img == nullptr) return {};
	// X server usually gives BGRA on little-endian; Format_RGB32 stores 0xffRRGGBB as BGRA
	// bytes in memory, matching. Alpha is undefined, so force opaque with RGB32.
	QImage q(reinterpret_cast<uchar*>(img->data), img->width, img->height, img->bytes_per_line, QImage::Format_RGB32);
	return q.copy();
}

}

// ── X11Source ───────────────────────────────────────────────────────────────────────

using QOverlay::Capture::Linux::X11Source;

X11Source::X11Source(Display* display, const X11Target& target, QObject* parent)
	: ICaptureSource(parent)
	, m_display(display)
	, m_target(target)
{
}

X11Source::~X11Source() {
	if (m_pixmap != 0 && m_display != nullptr) {
		XFreePixmap(m_display, static_cast<Pixmap>(m_pixmap));
	}
}

// (Re)name the window's backing pixmap. XComposite allocates a new one every time the window
// is resized, and the old name then still refers to the previous, smaller buffer — reading the
// new geometry from it is a BadMatch, which Xlib's default error handler turns into exit().
void X11Source::renamePixmap() {
	if (m_pixmap != 0) XFreePixmap(m_display, static_cast<Pixmap>(m_pixmap));
	m_pixmap = XCompositeNameWindowPixmap(m_display, static_cast<Window>(m_target.window));
	m_pixmapSize = QSize(m_target.w, m_target.h);
}

bool X11Source::start() {
	if (m_display == nullptr) return false;

	if (m_target.kind == CaptureSurface::Kind::Window) {
		// Redirect the window so its contents are available even when occluded, then name
		// its backing pixmap; submit() re-names it whenever the window is resized.
		XCompositeRedirectWindow(m_display, static_cast<Window>(m_target.window), CompositeRedirectAutomatic);
		renamePixmap();
	}

	m_size = QSize(m_target.w, m_target.h);
	m_started = m_size.isValid() && !m_size.isEmpty();
	if (m_started) emit frameSizeChanged(m_size);
	return m_started;
}

void X11Source::submit(vr::VROverlayHandle_t overlay) {
	if (!m_started || overlay == vr::k_ulOverlayHandleInvalid) return;

	Drawable drawable;
	int gx = 0, gy = 0, gw = m_target.w, gh = m_target.h;
	if (m_target.kind == CaptureSurface::Kind::Window) {
		// Track live window geometry so a resized window still captures fully.
		Window root; int rx, ry; unsigned int w, h, bw, depth;
		if (XGetGeometry(m_display, static_cast<Window>(m_target.window), &root, &rx, &ry, &w, &h, &bw, &depth)) {
			gw = static_cast<int>(w); gh = static_cast<int>(h);
		}
		if (QSize(gw, gh) != m_pixmapSize) {
			m_target.w = gw; m_target.h = gh;
			renamePixmap();
		}
		drawable = m_pixmap != 0 ? static_cast<Drawable>(m_pixmap) : static_cast<Drawable>(m_target.window);
	} else {
		drawable = DefaultRootWindow(m_display);
		gx = m_target.x; gy = m_target.y;
	}
	if (gw <= 0 || gh <= 0) return;

	// XGetImage is a CPU read; XShm would be the perf upgrade during bring-up.
	XImage* img = XGetImage(m_display, drawable, gx, gy, static_cast<unsigned>(gw), static_cast<unsigned>(gh), AllPlanes, ZPixmap);
	if (img == nullptr) return;

	const QSize size(img->width, img->height);
	if (size != m_size) { m_size = size; emit frameSizeChanged(m_size); }

	// Server BGRA → GL BGRA upload (submitter flags bgra=true).
	m_submitter.UploadAndSubmit(overlay, img->data, size, /*bgra=*/true);
	XDestroyImage(img);
}

bool X11Source::overlayPixelToRoot(const QPointF& pixel, int& outX, int& outY) const {
	if (m_size.isEmpty()) return false;
	int baseX = m_target.x, baseY = m_target.y, w = m_target.w, h = m_target.h;
	if (m_target.kind == CaptureSurface::Kind::Window) {
		// Window's current root-space origin.
		Window child; int rx = 0, ry = 0;
		XTranslateCoordinates(m_display, static_cast<Window>(m_target.window), DefaultRootWindow(m_display), 0, 0, &rx, &ry, &child);
		baseX = rx; baseY = ry;
	}
	const double u = std::clamp(pixel.x() / m_size.width(), 0.0, 1.0);
	const double v = std::clamp(pixel.y() / m_size.height(), 0.0, 1.0);
	outX = baseX + static_cast<int>(u * w);
	outY = baseY + static_cast<int>(v * h);
	return true;
}

void X11Source::injectMouse(Qt::MouseButton button, const QPointF& overlayPixel) {
	int x = 0, y = 0;
	if (!overlayPixelToRoot(overlayPixel, x, y)) return;

	if (button != Qt::NoButton && m_held == Qt::NoButton) emit interacted();

	// XTest is global: move the pointer, then press/release. (No per-window inject on X11.)
	XTestFakeMotionEvent(m_display, -1, x, y, CurrentTime);

	auto xbtn = [](Qt::MouseButton b) -> unsigned int {
		switch (b) { case Qt::LeftButton: return 1; case Qt::MiddleButton: return 2; case Qt::RightButton: return 3; default: return 0; }
	};
	if (button != m_held) {
		if (m_held != Qt::NoButton) XTestFakeButtonEvent(m_display, xbtn(m_held), False, CurrentTime);
		if (button != Qt::NoButton) XTestFakeButtonEvent(m_display, xbtn(button), True, CurrentTime);
		m_held = button;
	}
	XFlush(m_display);
}

void X11Source::mouseGone() {
	if (m_held == Qt::NoButton) return;
	auto xbtn = [](Qt::MouseButton b) -> unsigned int {
		switch (b) { case Qt::LeftButton: return 1; case Qt::MiddleButton: return 2; case Qt::RightButton: return 3; default: return 0; }
	};
	XTestFakeButtonEvent(m_display, xbtn(m_held), False, CurrentTime);
	m_held = Qt::NoButton;
	XFlush(m_display);
}

void X11Source::injectScroll(int notches, const QPointF& overlayPixel) {
	if (notches == 0) return;
	int x = 0, y = 0;
	if (!overlayPixelToRoot(overlayPixel, x, y)) return;

	// X11 has no wheel event as such: each detent is a click of button 4 (up) or 5 (down),
	// delivered to whatever is under the pointer — so move there first, like the monitor path
	// on Windows.
	XTestFakeMotionEvent(m_display, -1, x, y, CurrentTime);
	const unsigned int button = notches > 0 ? 4 : 5;
	for (int i = 0; i < std::abs(notches); ++i) {
		XTestFakeButtonEvent(m_display, button, True, CurrentTime);
		XTestFakeButtonEvent(m_display, button, False, CurrentTime);
	}
	XFlush(m_display);
}

void X11Source::injectText(const QString& text) {
	// XTest types by keycode, so map each character to a keysym → keycode. Characters not
	// on the current layout need a temporary keymap remap (XChangeKeyboardMapping) — left
	// as a bring-up refinement; ASCII covers the common case here.
	for (const QChar ch : text) {
		KeySym sym = static_cast<KeySym>(ch.unicode());
		KeyCode code = XKeysymToKeycode(m_display, sym);
		if (code == 0) continue;
		XTestFakeKeyEvent(m_display, code, True, CurrentTime);
		XTestFakeKeyEvent(m_display, code, False, CurrentTime);
	}
	XFlush(m_display);
}

void X11Source::injectKey(Qt::Key key, bool down) {
	KeySym sym = 0;
	switch (key) {
		case Qt::Key_Return: case Qt::Key_Enter: sym = XK_Return; break;
		case Qt::Key_Backspace: sym = XK_BackSpace; break;
		case Qt::Key_Tab: sym = XK_Tab; break;
		case Qt::Key_Escape: sym = XK_Escape; break;
		case Qt::Key_Left: sym = XK_Left; break;
		case Qt::Key_Right: sym = XK_Right; break;
		case Qt::Key_Up: sym = XK_Up; break;
		case Qt::Key_Down: sym = XK_Down; break;
		case Qt::Key_Delete: sym = XK_Delete; break;
		case Qt::Key_Home: sym = XK_Home; break;
		case Qt::Key_End: sym = XK_End; break;
		default: return;
	}
	KeyCode code = XKeysymToKeycode(m_display, sym);
	if (code == 0) return;
	XTestFakeKeyEvent(m_display, code, down ? True : False, CurrentTime);
	XFlush(m_display);
}

// ── X11Backend ──────────────────────────────────────────────────────────────────────

using QOverlay::Capture::Linux::X11Backend;

bool X11Backend::isAvailable() {
	Display* dpy = XOpenDisplay(nullptr);
	if (dpy == nullptr) return false;
	XCloseDisplay(dpy);
	return true;
}

X11Backend::X11Backend(QObject* parent)
	: ICaptureBackend(parent)
	, m_display(XOpenDisplay(nullptr))
{
	if (m_display == nullptr) {
		fmt::print("X11Backend: XOpenDisplay failed\n");
	}
}

X11Backend::~X11Backend() {
	if (m_display != nullptr) XCloseDisplay(m_display);
}

QList<QOverlay::Capture::CaptureSurface> X11Backend::enumerateSurfaces() {
	m_targets.clear();
	if (m_display == nullptr) return {};

	Window root = DefaultRootWindow(m_display);

	// Monitors via RandR active CRTCs.
	if (XRRScreenResources* res = XRRGetScreenResources(m_display, root)) {
		for (int i = 0; i < res->ncrtc; ++i) {
			XRRCrtcInfo* crtc = XRRGetCrtcInfo(m_display, res, res->crtcs[i]);
			if (crtc && crtc->mode != 0 && crtc->width > 0 && crtc->height > 0) {
				X11Target t;
				t.kind = CaptureSurface::Kind::Monitor;
				t.id = QString("monitor:%1").arg(m_targets.size());
				t.title = QString("Display %1 (%2x%3)").arg(m_targets.size() + 1).arg(crtc->width).arg(crtc->height);
				t.window = root;
				t.x = crtc->x; t.y = crtc->y; t.w = static_cast<int>(crtc->width); t.h = static_cast<int>(crtc->height);
				m_targets.push_back(t);
			}
			if (crtc) XRRFreeCrtcInfo(crtc);
		}
		XRRFreeScreenResources(res);
	}

	// Windows via _NET_CLIENT_LIST (managed top-level windows).
	Atom clientList = XInternAtom(m_display, "_NET_CLIENT_LIST", True);
	if (clientList != None) {
		Atom type = 0; int fmt = 0; unsigned long n = 0, after = 0; unsigned char* data = nullptr;
		if (XGetWindowProperty(m_display, root, clientList, 0, 4096, False, XA_WINDOW, &type, &fmt, &n, &after, &data) == Success && data) {
			auto* wins = reinterpret_cast<Window*>(data);
			for (unsigned long i = 0; i < n; ++i) {
				QString title = WindowTitle(m_display, wins[i]);
				if (title.isEmpty()) continue;
				XWindowAttributes attr;
				if (!XGetWindowAttributes(m_display, wins[i], &attr) || attr.map_state != IsViewable) continue;

				X11Target t;
				t.kind = CaptureSurface::Kind::Window;
				t.id = HandleId("window", wins[i]);
				t.title = title;
				t.appName = WindowClass(m_display, wins[i]);
				t.window = wins[i];
				t.w = attr.width; t.h = attr.height;
				m_targets.push_back(t);
			}
			XFree(data);
		}
	}

	QList<CaptureSurface> out;
	for (const auto& t : m_targets) out.append(CaptureSurface{ t.id, t.title, t.appName, t.kind });
	return out;
}

const X11Target* X11Backend::find(const QString& id) const {
	for (const auto& t : m_targets) if (t.id == id) return &t;
	return nullptr;
}

QImage X11Backend::grabThumbnail(const QString& surfaceId, int maxDim) {
	const X11Target* t = find(surfaceId);
	if (t == nullptr || m_display == nullptr) return {};

	Drawable drawable = (t->kind == CaptureSurface::Kind::Window) ? static_cast<Drawable>(t->window) : DefaultRootWindow(m_display);
	int x = (t->kind == CaptureSurface::Kind::Window) ? 0 : t->x;
	int y = (t->kind == CaptureSurface::Kind::Window) ? 0 : t->y;
	XImage* img = XGetImage(m_display, drawable, x, y, static_cast<unsigned>(t->w), static_cast<unsigned>(t->h), AllPlanes, ZPixmap);
	QImage q = ImageFromX(img);
	if (img) XDestroyImage(img);
	if (q.isNull()) return {};
	return q.scaled(maxDim, maxDim, Qt::KeepAspectRatio, Qt::SmoothTransformation);
}

QImage X11Backend::grabIcon(const QString& surfaceId, int maxDim) {
	const X11Target* t = find(surfaceId);
	if (t == nullptr || m_display == nullptr || t->kind != CaptureSurface::Kind::Window) return {};
	return WindowIcon(m_display, static_cast<Window>(t->window), maxDim);
}

QOverlay::Capture::ICaptureSource* X11Backend::createSource(const QString& surfaceId, QObject* parent) {
	const X11Target* t = find(surfaceId);
	if (t == nullptr) return nullptr;
	auto* src = new X11Source(m_display, *t, parent);
	if (!src->start()) { delete src; return nullptr; }
	return src;
}
