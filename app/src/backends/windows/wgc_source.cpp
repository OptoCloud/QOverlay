#include "backends/windows/wgc_source.h"

#include "input/desktop_input.h"

#include <fmt/core.h>

#include <algorithm>
#include <array>

#include <d3d11.h>
#include <dwmapi.h>
#include <openvr.h>

namespace {

// WGC captures a window at its DWM "extended frame bounds" (the visible frame, excluding
// the invisible resize border), so map into that rather than GetWindowRect for accuracy.
RECT WindowCaptureRect(HWND hwnd) {
	RECT rect{};
	if (SUCCEEDED(DwmGetWindowAttribute(hwnd, DWMWA_EXTENDED_FRAME_BOUNDS, &rect, sizeof(rect)))
		&& rect.right > rect.left && rect.bottom > rect.top) {
		return rect;
	}
	GetWindowRect(hwnd, &rect);
	return rect;
}

// Deepest visible child window at a screen point, walking down the target's own hierarchy
// so input reaches the actual control (e.g. a browser's render widget) regardless of what
// is in front of it on the real desktop.
HWND DeepChildAt(HWND root, POINT screenPt) {
	HWND current = root;
	for (int guard = 0; guard < 16; ++guard) {
		POINT client = screenPt;
		ScreenToClient(current, &client);
		HWND child = ChildWindowFromPointEx(current, client,
			CWP_SKIPINVISIBLE | CWP_SKIPTRANSPARENT | CWP_SKIPDISABLED);
		if (child == nullptr || child == current) break;
		current = child;
	}
	return current;
}

UINT ButtonDownMsg(Qt::MouseButton b) {
	switch (b) {
		case Qt::LeftButton:   return WM_LBUTTONDOWN;
		case Qt::RightButton:  return WM_RBUTTONDOWN;
		case Qt::MiddleButton: return WM_MBUTTONDOWN;
		default:               return 0;
	}
}
UINT ButtonUpMsg(Qt::MouseButton b) {
	switch (b) {
		case Qt::LeftButton:   return WM_LBUTTONUP;
		case Qt::RightButton:  return WM_RBUTTONUP;
		case Qt::MiddleButton: return WM_MBUTTONUP;
		default:               return 0;
	}
}
WPARAM ButtonMask(Qt::MouseButton b) {
	switch (b) {
		case Qt::LeftButton:   return MK_LBUTTON;
		case Qt::RightButton:  return MK_RBUTTON;
		case Qt::MiddleButton: return MK_MBUTTON;
		default:               return 0;
	}
}

// Windows virtual-key for the non-printable Qt keys a virtual keyboard emits (for posting
// WM_KEYDOWN/WM_KEYUP to a specific window). Printable text goes through WM_CHAR instead.
WORD QtKeyToVk(Qt::Key key) {
	if (key >= Qt::Key_F1 && key <= Qt::Key_F12) return static_cast<WORD>(VK_F1 + (key - Qt::Key_F1));
	switch (key) {
		case Qt::Key_Return:
		case Qt::Key_Enter:      return VK_RETURN;
		case Qt::Key_Backspace:  return VK_BACK;
		case Qt::Key_Tab:        return VK_TAB;
		case Qt::Key_Escape:     return VK_ESCAPE;
		case Qt::Key_Delete:     return VK_DELETE;
		case Qt::Key_Insert:     return VK_INSERT;
		case Qt::Key_Left:       return VK_LEFT;
		case Qt::Key_Right:      return VK_RIGHT;
		case Qt::Key_Up:         return VK_UP;
		case Qt::Key_Down:       return VK_DOWN;
		case Qt::Key_Home:       return VK_HOME;
		case Qt::Key_End:        return VK_END;
		case Qt::Key_PageUp:     return VK_PRIOR;
		case Qt::Key_PageDown:   return VK_NEXT;
		case Qt::Key_Print:      return VK_SNAPSHOT;
		case Qt::Key_ScrollLock: return VK_SCROLL;
		case Qt::Key_Pause:      return VK_PAUSE;
		case Qt::Key_Meta:       return VK_LWIN; // Windows key
		case Qt::Key_Menu:       return VK_APPS; // context-menu key
		default:                 return 0;
	}
}

}

using QOverlay::Capture::Win::WgcSource;

WgcSource::WgcSource(const CaptureTarget& target, QObject* parent)
	: ICaptureSource(parent)
	, m_kind(target.kind)
	, m_monitor(target.monitor)
	, m_window(target.window)
{
}

WgcSource::~WgcSource() {
	// Release any button we were injecting as "down" before we go away — otherwise closing
	// or retargeting an overlay mid-click leaves the OS button latched down system-wide
	// (the whole desktop becomes a drag-select).
	mouseGone();
	// Stop the capture session before the D3D device it renders onto goes away.
	m_pool.Stop();
}

bool WgcSource::createDevice() {
	if (m_device != nullptr) return true;

	// BGRA support is required so the device can back the WGC B8G8R8A8 frame pool.
	UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
	const std::array levels = { D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0 };

	const HRESULT hr = D3D11CreateDevice(
		nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags,
		levels.data(), static_cast<UINT>(levels.size()), D3D11_SDK_VERSION,
		m_device.put(), nullptr, m_context.put());

	if (FAILED(hr)) {
		fmt::print("WgcSource: D3D11CreateDevice failed (0x{:08x})\n", static_cast<unsigned>(hr));
		return false;
	}
	return true;
}

bool WgcSource::start() {
	if (!createDevice()) return false;

	const bool ok = (m_kind == CaptureSurface::Kind::Monitor)
		? m_pool.StartMonitor(m_monitor, m_device.get())
		: m_pool.StartWindow(m_window, m_device.get());
	if (!ok) {
		fmt::print("WgcSource: failed to start capture\n");
		return false;
	}

	// The WGC item size is known immediately, so the overlay aspect ratio is correct
	// before the first frame lands.
	m_size = QSize(static_cast<int>(m_pool.Width()), static_cast<int>(m_pool.Height()));
	emit frameSizeChanged(m_size);
	return true;
}

void WgcSource::submit(vr::VROverlayHandle_t overlay) {
	if (!isReady() || overlay == vr::k_ulOverlayHandleInvalid) return;

	m_pool.Update();
	ID3D11Texture2D* texture = m_pool.LatestFrame();
	if (texture == nullptr) return; // no frame captured yet

	auto ov = vr::VROverlay();
	if (ov == nullptr) return;

	vr::Texture_t tex{};
	tex.handle = texture;
	tex.eType = vr::TextureType_DirectX;
	tex.eColorSpace = vr::ColorSpace_Auto;

	const vr::EVROverlayError error = ov->SetOverlayTexture(overlay, &tex);

	if (!m_loggedSubmit && error == vr::VROverlayError_None) {
		m_loggedSubmit = true;
		fmt::print("WgcSource: first frame submitted ({}x{})\n", m_size.width(), m_size.height());
	}

	if (error != vr::VROverlayError_None) {
		if (m_submitErrors == 0 || (m_submitErrors % 100) == 0) {
			fmt::print("WgcSource: SetOverlayTexture failed ({}x): {}\n",
				m_submitErrors + 1, ov->GetOverlayErrorNameFromEnum(error));
		}
		++m_submitErrors;
	} else if (m_submitErrors > 0) {
		fmt::print("WgcSource: SetOverlayTexture recovered after {} failures\n", m_submitErrors);
		m_submitErrors = 0;
	}
}

bool WgcSource::overlayPixelToDesktop(const QPointF& pixel, int& outX, int& outY) const {
	if (m_size.isEmpty()) return false;

	// Live target rect so a window that was moved/resized since spawn still maps right.
	RECT rect{};
	if (m_kind == CaptureSurface::Kind::Monitor) {
		MONITORINFO info{};
		info.cbSize = sizeof(info);
		if (!GetMonitorInfoW(m_monitor, &info)) return false;
		rect = info.rcMonitor;
	} else {
		if (m_window == nullptr) return false;
		rect = WindowCaptureRect(m_window);
	}

	const float rw = static_cast<float>(rect.right - rect.left);
	const float rh = static_cast<float>(rect.bottom - rect.top);
	if (rw < 1.0f || rh < 1.0f) return false;

	const float px = std::clamp(static_cast<float>(pixel.x()), 0.0f, static_cast<float>(m_size.width()));
	const float py = std::clamp(static_cast<float>(pixel.y()), 0.0f, static_cast<float>(m_size.height()));
	outX = rect.left + static_cast<int>(px * rw / static_cast<float>(m_size.width()));
	outY = rect.top  + static_cast<int>(py * rh / static_cast<float>(m_size.height()));
	return true;
}

void WgcSource::injectMouse(Qt::MouseButton button, const QPointF& overlayPixel) {
	// Click-vs-drag deadzone (in overlay pixels ~= desktop pixels). While a button is held,
	// keep the cursor at the press anchor until the pointer moves past this radius; only
	// then does it become a real drag. Bigger than Windows' own drag threshold to absorb
	// VR-pointer jitter.
	constexpr double kDragThreshold = 16.0;

	// A fresh press: anchor here and start as a click (not yet dragging).
	if (button != Qt::NoButton && m_heldButton == Qt::NoButton) {
		m_pressAnchor = overlayPixel;
		m_dragging = false;
		emit interacted(); // focus-follow: this target now receives keyboard input
	}

	// Choose the effective pixel: while in a press cycle and not yet dragging, pin to the
	// anchor (so press/hold/release all land on the same spot = a clean click).
	QPointF pixel = overlayPixel;
	if (button != Qt::NoButton || m_heldButton != Qt::NoButton) {
		if (!m_dragging) {
			const double dx = overlayPixel.x() - m_pressAnchor.x();
			const double dy = overlayPixel.y() - m_pressAnchor.y();
			if (dx * dx + dy * dy >= kDragThreshold * kDragThreshold) m_dragging = true;
		}
		pixel = m_dragging ? overlayPixel : m_pressAnchor;
	}

	int x = 0, y = 0;
	if (!overlayPixelToDesktop(pixel, x, y)) return;

	if (m_kind == CaptureSurface::Kind::Monitor) {
		// A monitor overlay maps to real screen space, so drive the global cursor: what's
		// shown on that monitor is exactly what a click there should hit.
		Input::MoveCursor(x, y);
		if (button != m_heldButton) {
			if (m_heldButton != Qt::NoButton) Input::ReleaseButton(m_heldButton);
			if (button != Qt::NoButton) Input::PressButton(button);
			m_heldButton = button;
		}
		return;
	}

	// Move the global cursor to the mapped screen point so WGC's cursor capture renders it
	// in the overlay — but DON'T click globally: clicks are posted straight to the target
	// window so they hit that window regardless of desktop occlusion.
	Input::MoveCursor(x, y);

	const POINT screenPt{ x, y };
	// While a button is held, keep posting to the window that received the button-down: a
	// drag can cross child-window boundaries, and re-resolving would strand the down-target
	// with a press that never gets its matching up. Only re-resolve when nothing is held.
	HWND target = (m_heldButton != Qt::NoButton && m_downTarget != nullptr)
		? m_downTarget
		: DeepChildAt(m_window, screenPt);
	POINT client = screenPt;
	ScreenToClient(target, &client);
	const LPARAM lparam = MAKELPARAM(client.x, client.y);

	// Move carries the currently-held button mask so drags register as drags.
	PostMessageW(target, WM_MOUSEMOVE, ButtonMask(m_heldButton), lparam);

	if (button != m_heldButton) {
		if (m_heldButton != Qt::NoButton) {
			PostMessageW(target, ButtonUpMsg(m_heldButton), 0, lparam);
		}
		if (button != Qt::NoButton) {
			m_downTarget = target; // pin the whole press to this window
			PostMessageW(target, ButtonDownMsg(button), ButtonMask(button), lparam);
		} else {
			m_downTarget = nullptr;
		}
		m_heldButton = button;
	}

	m_lastWndTarget = target;
	m_lastWndLParam = lparam;
}

void WgcSource::mouseGone() {
	// Pointer left the overlay — release any button we were holding so it doesn't stick.
	if (m_heldButton == Qt::NoButton) return;

	if (m_kind == CaptureSurface::Kind::Monitor) {
		Input::ReleaseButton(m_heldButton);
	} else {
		HWND target = m_downTarget != nullptr ? m_downTarget : m_lastWndTarget;
		if (target != nullptr) PostMessageW(target, ButtonUpMsg(m_heldButton), 0, m_lastWndLParam);
	}
	m_heldButton = Qt::NoButton;
	m_downTarget = nullptr;
}

void WgcSource::injectText(const QString& text) {
	if (m_kind == CaptureSurface::Kind::Monitor) {
		// Whatever the user last clicked on that monitor has focus — type globally.
		Input::TypeText(text);
		return;
	}

	// Post characters straight to the window's last-focused control so occlusion and app
	// focus don't matter.
	HWND target = (m_lastWndTarget != nullptr) ? m_lastWndTarget : m_window;
	if (target == nullptr) return;
	for (const QChar ch : text) {
		PostMessageW(target, WM_CHAR, static_cast<WPARAM>(ch.unicode()), 0);
	}
}

void WgcSource::injectKey(Qt::Key key, bool down) {
	if (m_kind == CaptureSurface::Kind::Monitor) {
		Input::KeyEvent(key, down);
		return;
	}

	HWND target = (m_lastWndTarget != nullptr) ? m_lastWndTarget : m_window;
	if (target == nullptr) return;
	const WORD vk = QtKeyToVk(key);
	if (vk == 0) return;
	PostMessageW(target, down ? WM_KEYDOWN : WM_KEYUP, vk, 0);
}
