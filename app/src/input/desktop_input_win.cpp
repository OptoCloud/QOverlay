#include "input/desktop_input.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

namespace {

void SendMouse(DWORD flags, LONG dx = 0, LONG dy = 0, DWORD data = 0) {
	INPUT input{};
	input.type = INPUT_MOUSE;
	input.mi.dx = dx;
	input.mi.dy = dy;
	input.mi.mouseData = data;
	input.mi.dwFlags = flags;
	SendInput(1, &input, sizeof(INPUT));
}

}

void QOverlay::Input::MoveCursor(int desktopX, int desktopY) {
	// SendInput absolute coordinates are normalized to [0,65535] across the whole virtual
	// desktop, so convert from the virtual-screen pixel origin/extent.
	const int originX = GetSystemMetrics(SM_XVIRTUALSCREEN);
	const int originY = GetSystemMetrics(SM_YVIRTUALSCREEN);
	const int width = GetSystemMetrics(SM_CXVIRTUALSCREEN);
	const int height = GetSystemMetrics(SM_CYVIRTUALSCREEN);
	if (width <= 1 || height <= 1) return;

	const LONG nx = static_cast<LONG>((static_cast<double>(desktopX - originX) * 65535.0) / (width - 1));
	const LONG ny = static_cast<LONG>((static_cast<double>(desktopY - originY) * 65535.0) / (height - 1));
	SendMouse(MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK, nx, ny);
}

void QOverlay::Input::PressButton(Qt::MouseButton button) {
	switch (button) {
		case Qt::LeftButton:   SendMouse(MOUSEEVENTF_LEFTDOWN); break;
		case Qt::RightButton:  SendMouse(MOUSEEVENTF_RIGHTDOWN); break;
		case Qt::MiddleButton: SendMouse(MOUSEEVENTF_MIDDLEDOWN); break;
		default: break;
	}
}

void QOverlay::Input::ReleaseButton(Qt::MouseButton button) {
	switch (button) {
		case Qt::LeftButton:   SendMouse(MOUSEEVENTF_LEFTUP); break;
		case Qt::RightButton:  SendMouse(MOUSEEVENTF_RIGHTUP); break;
		case Qt::MiddleButton: SendMouse(MOUSEEVENTF_MIDDLEUP); break;
		default: break;
	}
}

void QOverlay::Input::Scroll(int notches) {
	SendMouse(MOUSEEVENTF_WHEEL, 0, 0, static_cast<DWORD>(notches * WHEEL_DELTA));
}

namespace {

void SendUnicode(wchar_t ch, bool down) {
	INPUT input{};
	input.type = INPUT_KEYBOARD;
	input.ki.wScan = ch;
	input.ki.dwFlags = KEYEVENTF_UNICODE | (down ? 0 : KEYEVENTF_KEYUP);
	SendInput(1, &input, sizeof(INPUT));
}

void SendVirtualKey(WORD vk, bool down) {
	INPUT input{};
	input.type = INPUT_KEYBOARD;
	input.ki.wVk = vk;
	input.ki.dwFlags = down ? 0 : KEYEVENTF_KEYUP;
	SendInput(1, &input, sizeof(INPUT));
}

// Map the non-printable Qt keys a virtual keyboard emits to Windows virtual-key codes.
// Printable characters go through TypeText (Unicode) instead, so only control keys here.
WORD QtKeyToVk(Qt::Key key) {
	if (key >= Qt::Key_F1 && key <= Qt::Key_F12) return static_cast<WORD>(VK_F1 + (key - Qt::Key_F1));
	switch (key) {
		case Qt::Key_Return:
		case Qt::Key_Enter:      return VK_RETURN;
		case Qt::Key_Backspace:  return VK_BACK;
		case Qt::Key_Tab:        return VK_TAB;
		case Qt::Key_Escape:     return VK_ESCAPE;
		case Qt::Key_Space:      return VK_SPACE;
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
		case Qt::Key_Shift:      return VK_SHIFT;
		case Qt::Key_Control:    return VK_CONTROL;
		case Qt::Key_Alt:        return VK_MENU;
		case Qt::Key_Meta:       return VK_LWIN;
		case Qt::Key_Menu:       return VK_APPS;
		case Qt::Key_CapsLock:   return VK_CAPITAL;
		default:                 return 0;
	}
}

}

void QOverlay::Input::TypeText(const QString& text) {
	for (const QChar ch : text) {
		const wchar_t w = static_cast<wchar_t>(ch.unicode());
		SendUnicode(w, true);
		SendUnicode(w, false);
	}
}

void QOverlay::Input::KeyEvent(Qt::Key key, bool down) {
	if (const WORD vk = QtKeyToVk(key); vk != 0) {
		SendVirtualKey(vk, down);
	}
}
