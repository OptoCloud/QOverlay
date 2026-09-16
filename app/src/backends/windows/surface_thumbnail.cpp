#include "backends/windows/surface_thumbnail.h"

// PW_RENDERFULLCONTENT (Windows 8.1+) makes PrintWindow capture DirectComposition /
// hardware-accelerated windows (Chrome, UWP, etc.) instead of returning black.
#ifndef PW_RENDERFULLCONTENT
#define PW_RENDERFULLCONTENT 0x00000002
#endif

namespace {

// Read a GDI bitmap (already drawn into `mem`) back into a top-down 32-bit QImage. The
// bitmap must be deselected from every DC before GetDIBits, so we pass `mem` purely as a
// compatible DC and `bmp` as the (deselected) source.
QImage DibToImage(HDC mem, HBITMAP bmp, int w, int h) {
	BITMAPINFO bi{};
	bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
	bi.bmiHeader.biWidth = w;
	bi.bmiHeader.biHeight = -h; // negative => top-down
	bi.bmiHeader.biPlanes = 1;
	bi.bmiHeader.biBitCount = 32;
	bi.bmiHeader.biCompression = BI_RGB;

	QImage image(w, h, QImage::Format_RGB32);
	if (GetDIBits(mem, bmp, 0, static_cast<UINT>(h), image.bits(), &bi, DIB_RGB_COLORS) == 0) {
		return {};
	}
	return image;
}

QImage GrabWindow(HWND hwnd) {
	RECT rect{};
	if (!GetWindowRect(hwnd, &rect)) return {};
	const int w = rect.right - rect.left;
	const int h = rect.bottom - rect.top;
	if (w < 2 || h < 2) return {};

	HDC screen = GetDC(nullptr);
	HDC mem = CreateCompatibleDC(screen);
	HBITMAP bmp = CreateCompatibleBitmap(screen, w, h);
	HGDIOBJ old = SelectObject(mem, bmp);

	const BOOL ok = PrintWindow(hwnd, mem, PW_RENDERFULLCONTENT);

	SelectObject(mem, old); // deselect before GetDIBits
	QImage image = ok ? DibToImage(mem, bmp, w, h) : QImage();

	DeleteObject(bmp);
	DeleteDC(mem);
	ReleaseDC(nullptr, screen);
	return image;
}

// Read a GDI bitmap into a top-down 32-bit ARGB QImage (keeps the source alpha channel,
// unlike the RGB helper above). Returns null on failure.
QImage BitmapToArgb(HBITMAP bmp, int w, int h) {
	BITMAPINFO bi{};
	bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
	bi.bmiHeader.biWidth = w;
	bi.bmiHeader.biHeight = -h; // top-down
	bi.bmiHeader.biPlanes = 1;
	bi.bmiHeader.biBitCount = 32;
	bi.bmiHeader.biCompression = BI_RGB;

	QImage image(w, h, QImage::Format_ARGB32);
	HDC screen = GetDC(nullptr);
	const int scanned = GetDIBits(screen, bmp, 0, static_cast<UINT>(h), image.bits(), &bi, DIB_RGB_COLORS);
	ReleaseDC(nullptr, screen);
	return scanned == 0 ? QImage() : image;
}

QImage IconToImage(HICON icon) {
	if (icon == nullptr) return {};

	ICONINFO info{};
	if (!GetIconInfo(icon, &info)) return {};

	BITMAP bm{};
	GetObjectW(info.hbmColor, sizeof(bm), &bm);
	const int w = bm.bmWidth;
	const int h = bm.bmHeight;

	QImage image = (w > 0 && h > 0) ? BitmapToArgb(info.hbmColor, w, h) : QImage();

	// Legacy icons carry no alpha (the color bitmap is fully opaque, transparency lives in the
	// 1-bpp mask). Detect an all-zero alpha channel and rebuild it from the mask: where the
	// mask bit is 0 the pixel is opaque, where 1 it's transparent.
	if (!image.isNull()) {
		bool anyAlpha = false;
		for (int y = 0; y < h && !anyAlpha; ++y) {
			const QRgb* row = reinterpret_cast<const QRgb*>(image.constScanLine(y));
			for (int x = 0; x < w; ++x) { if (qAlpha(row[x]) != 0) { anyAlpha = true; break; } }
		}
		if (!anyAlpha && info.hbmMask != nullptr) {
			const QImage mask = BitmapToArgb(info.hbmMask, w, h);
			for (int y = 0; y < h; ++y) {
				QRgb* row = reinterpret_cast<QRgb*>(image.scanLine(y));
				const QRgb* mrow = mask.isNull() ? nullptr : reinterpret_cast<const QRgb*>(mask.constScanLine(y));
				for (int x = 0; x < w; ++x) {
					const bool transparent = mrow != nullptr && (qRed(mrow[x]) > 127);
					row[x] = transparent ? (row[x] & 0x00FFFFFF) : (row[x] | 0xFF000000);
				}
			}
		}
	}

	if (info.hbmColor != nullptr) DeleteObject(info.hbmColor);
	if (info.hbmMask != nullptr) DeleteObject(info.hbmMask);
	return image;
}

HICON WindowIcon(HWND hwnd) {
	// Prefer the window's own big icon; fall back through the small icons and the window class.
	DWORD_PTR result = 0;
	for (const WPARAM which : { ICON_BIG, ICON_SMALL2, ICON_SMALL }) {
		if (SendMessageTimeoutW(hwnd, WM_GETICON, which, 0, SMTO_ABORTIFHUNG, 200, &result) && result != 0) {
			return reinterpret_cast<HICON>(result);
		}
	}
	if (HICON cls = reinterpret_cast<HICON>(GetClassLongPtrW(hwnd, GCLP_HICON))) return cls;
	if (HICON cls = reinterpret_cast<HICON>(GetClassLongPtrW(hwnd, GCLP_HICONSM))) return cls;
	return nullptr;
}

QImage GrabMonitor(HMONITOR monitor) {
	MONITORINFO info{};
	info.cbSize = sizeof(info);
	if (!GetMonitorInfoW(monitor, &info)) return {};
	const int x = info.rcMonitor.left;
	const int y = info.rcMonitor.top;
	const int w = info.rcMonitor.right - info.rcMonitor.left;
	const int h = info.rcMonitor.bottom - info.rcMonitor.top;
	if (w < 2 || h < 2) return {};

	HDC screen = GetDC(nullptr);
	HDC mem = CreateCompatibleDC(screen);
	HBITMAP bmp = CreateCompatibleBitmap(screen, w, h);
	HGDIOBJ old = SelectObject(mem, bmp);

	const BOOL ok = BitBlt(mem, 0, 0, w, h, screen, x, y, SRCCOPY);

	SelectObject(mem, old);
	QImage image = ok ? DibToImage(mem, bmp, w, h) : QImage();

	DeleteObject(bmp);
	DeleteDC(mem);
	ReleaseDC(nullptr, screen);
	return image;
}

}

QImage QOverlay::Capture::Win::GrabThumbnail(const CaptureTarget& target, int maxDim) {
	QImage image = (target.kind == CaptureSurface::Kind::Monitor)
		? GrabMonitor(target.monitor)
		: GrabWindow(target.window);
	if (image.isNull()) return {};

	return image.scaled(maxDim, maxDim, Qt::KeepAspectRatio, Qt::SmoothTransformation);
}

QImage QOverlay::Capture::Win::GrabIcon(const CaptureTarget& target, int maxDim) {
	if (target.kind != CaptureSurface::Kind::Window || target.window == nullptr) return {};

	QImage image = IconToImage(WindowIcon(target.window));
	if (image.isNull()) return {};

	return image.scaled(maxDim, maxDim, Qt::KeepAspectRatio, Qt::SmoothTransformation);
}
