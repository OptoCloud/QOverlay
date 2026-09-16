#pragma once

#include <QString>
#include <QtCore/qnamespace.h>

// Platform-neutral desktop input injection. The declarations here take only plain
// coordinates and Qt button enums — no OS types — so a non-Windows backend (X11 XTest /
// uinput on Linux) can implement the same surface without touching callers. Exactly one
// implementation TU is compiled per platform (desktop_input_win.cpp on Windows).
namespace QOverlay::Input {

// Move the system cursor to an absolute pixel on the virtual desktop (the union of all
// monitors; coordinates may be negative on secondary displays).
void MoveCursor(int desktopX, int desktopY);

// Press / release a mouse button at the current cursor position. Left/Right/Middle are
// supported; other buttons are ignored.
void PressButton(Qt::MouseButton button);
void ReleaseButton(Qt::MouseButton button);

// Vertical wheel scroll. `notches` is signed (+up / -down); one notch == one detent.
void Scroll(int notches);

// Type Unicode text into whatever currently has keyboard focus (printable characters).
void TypeText(const QString& text);

// Press/release a single key by Qt key code — for non-printable keys (Enter, Backspace,
// Tab, arrows, modifiers, …). Printable characters should go through TypeText.
void KeyEvent(Qt::Key key, bool down);

}
