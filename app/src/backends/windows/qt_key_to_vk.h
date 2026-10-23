#pragma once

// Windows-only. Shared Qt::Key -> Win32 virtual-key mapping for the two places that need
// to post/send a non-printable key: the global desktop-input backend (SendInput) and the
// per-window WGC capture source (PostMessage). Kept as one superset table so a key added
// for one path isn't silently missing from the other.

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

#include <QtCore/qnamespace.h>

namespace QOverlay::Input::Win {

inline WORD QtKeyToVk(Qt::Key key) {
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
		case Qt::Key_Meta:       return VK_LWIN; // Windows key
		case Qt::Key_Menu:       return VK_APPS; // context-menu key
		case Qt::Key_CapsLock:   return VK_CAPITAL;
		default:                 return 0;
	}
}

}
