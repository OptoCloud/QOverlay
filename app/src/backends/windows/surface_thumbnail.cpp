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
