#pragma once

#include <cstdint>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <d3d11.h>

#include <winrt/base.h>
#include <winrt/Windows.Graphics.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>

namespace QOverlay::Capture::Win {

// Low-level Windows.Graphics.Capture frame producer for one target (monitor or window).
// Owns a Direct3D11CaptureFramePool + GraphicsCaptureSession and keeps the newest frame
// copied into a persistent shareable BGRA texture that WgcSource submits to OpenVR.
//
// Polled, not event-driven: call Update() once per VR frame from the main thread. We
// deliberately avoid the FrameArrived event (and its threadpool + D3D context locking)
// so the whole capture path stays single-threaded.
class WgcFramePool {
public:
	WgcFramePool() = default;
	~WgcFramePool();

	WgcFramePool(const WgcFramePool&) = delete;
	WgcFramePool& operator=(const WgcFramePool&) = delete;

	// Begin capturing `monitor`/`window`. `device` is the D3D11 device the captured frames
	// and the persistent copy target live on — the same device WgcSource submits from. The
	// OS cursor is captured (WGC default) so the overlay shows the cursor we drive.
	bool StartMonitor(HMONITOR monitor, ID3D11Device* device);
	bool StartWindow(HWND window, ID3D11Device* device);

	void Stop();

	// Drain pending WGC frames and copy the newest into the persistent texture.
	// Returns true if a new frame was captured this call.
	bool Update();

	// The latest captured frame as a shareable D3D11 texture, or nullptr if none yet.
	ID3D11Texture2D* LatestFrame() const { return m_frame.get(); }

	std::uint32_t Width() const { return m_width; }
	std::uint32_t Height() const { return m_height; }
	bool Ok() const { return m_session != nullptr; }

private:
	// Wrap our D3D device for WinRT and start a capture session for `item`.
	bool StartItem(const winrt::Windows::Graphics::Capture::GraphicsCaptureItem& item, ID3D11Device* device);
	// (Re)create the persistent copy target and the frame pool for a new content size.
	bool EnsureFrameTexture(std::uint32_t width, std::uint32_t height);
	void RecreateFramePool(std::uint32_t width, std::uint32_t height);

	winrt::Windows::Graphics::Capture::GraphicsCaptureItem m_item{ nullptr };
	winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool m_framePool{ nullptr };
	winrt::Windows::Graphics::Capture::GraphicsCaptureSession m_session{ nullptr };
	winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice m_winrtDevice{ nullptr };

	winrt::com_ptr<ID3D11Device> m_device;
	winrt::com_ptr<ID3D11DeviceContext> m_context;
	winrt::com_ptr<ID3D11Texture2D> m_frame; // persistent, shareable copy of the newest frame

	std::uint32_t m_width = 0;
	std::uint32_t m_height = 0;
	std::uint64_t m_updateErrors = 0; // rate-limit transient capture-failure spam
};

}
