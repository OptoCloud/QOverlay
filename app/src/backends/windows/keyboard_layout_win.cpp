// OS keyboard-layout legend query, split out of wgc_backend.cpp so the keyboard tester tool
// can link it without pulling in the whole WGC/D3D capture stack. Windows-only.
#include "capture/backend_factory.h"

#include <windows.h>

#include <QString>
#include <QVariantMap>

quint64 QOverlay::Capture::currentKeyboardLayoutToken() {
	HWND fg = GetForegroundWindow();
	HKL hkl = GetKeyboardLayout(fg ? GetWindowThreadProcessId(fg, nullptr) : 0);
	return reinterpret_cast<quint64>(hkl); // opaque; only used for change detection
}

QVariantMap QOverlay::Capture::currentKeyboardLegends() {
	// The layout of whatever window is focused (what typing there would produce).
	HWND fg = GetForegroundWindow();
	HKL hkl = GetKeyboardLayout(fg ? GetWindowThreadProcessId(fg, nullptr) : 0);

	// Legend for one physical key (set-1 scancode). `n` = unshifted, `s` = shifted.
	auto legend = [&](UINT scancode, bool shift) -> QString {
		const UINT vk = MapVirtualKeyExW(scancode, MAPVK_VSC_TO_VK_EX, hkl);
		if (vk == 0) return {};
		BYTE keyState[256] = {};
		if (shift) keyState[VK_SHIFT] = 0x80;
		wchar_t buffer[8] = {};
		const int n = ToUnicodeEx(vk, scancode, keyState, buffer, 8, 0, hkl);
		const QString result = (n != 0) ? QString::fromWCharArray(buffer, 1) : QString();
		// A dead key (n < 0, e.g. the Nordic ¨/^/~ key) leaves a pending diacritic buffered in
		// the kernel; if we don't flush it, the NEXT scancode we query gets composed with it and
		// comes back blank or wrong (the cause of scattered missing legends like a/b/g on
		// dead-key layouts). Consume it with a space so each query starts clean.
		if (n < 0) {
			wchar_t flush[8] = {};
			ToUnicodeEx(VK_SPACE, MapVirtualKeyExW(VK_SPACE, MAPVK_VK_TO_VSC, hkl), keyState, flush, 8, 0, hkl);
		}
		return result; // first char (also the dead-key diacritic)
	};

	// All character scancodes we expose. The QML owns the physical arrangement (ANSI vs ISO)
	// and looks these up by scancode, so we just hand over the data. 0x2B and 0x56 are the
	// keys that move/appear between the two physical layouts.
	static const UINT scancodes[] = {
		0x29, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, // number row
		0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1A, 0x1B,       // qwerty row
		0x1E, 0x1F, 0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28,             // home row
		0x2C, 0x2D, 0x2E, 0x2F, 0x30, 0x31, 0x32, 0x33, 0x34, 0x35,                   // bottom row
		0x2B, 0x56,                                                                   // \ / ISO <>|
	};

	QVariantMap keys;
	for (UINT scancode : scancodes) {
		QVariantMap key;
		key["n"] = legend(scancode, false);
		key["s"] = legend(scancode, true);
		keys[QString::number(scancode)] = key;
	}

	// ISO keyboards have the extra key (scancode 0x56, the <>| key) left of Z; ANSI don't.
	// The QML uses this to pick the ISO physical layout (tall Enter, that extra key, ' on the
	// home row) vs the ANSI one.
	QVariantMap out;
	out["iso"] = !legend(0x56, false).isEmpty();
	out["k"] = keys;
	return out;
}
