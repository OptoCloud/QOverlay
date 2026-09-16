#include "backends/windows/wgc_frame_pool.h"

#include <fmt/core.h>

#include <dxgi.h>

// Interop between classic D3D11/DXGI and the WinRT Windows.Graphics.Capture surface.
#include <windows.graphics.capture.interop.h>
#include <windows.graphics.directx.direct3d11.interop.h>
#include <winrt/Windows.Foundation.h>            // IClosable::Close consume methods
#include <winrt/Windows.Foundation.Metadata.h>   // ApiInformation feature detection

// The bridge interface from a WinRT IDirect3DSurface/Device back to its underlying DXGI
// object. The Windows SDK interop header doesn't reliably export it, so declare it here
// (same pattern as the Microsoft WGC samples).
extern "C" {
struct __declspec(uuid("A9B3D012-3DF2-4EE3-B8D1-8695F457D3C1")) IDirect3DDxgiInterfaceAccess : ::IUnknown {
	virtual HRESULT __stdcall GetInterface(REFIID iid, void** object) = 0;
};
}

namespace winrt {
using namespace winrt::Windows::Graphics;
using namespace winrt::Windows::Graphics::Capture;
using namespace winrt::Windows::Graphics::DirectX;
using namespace winrt::Windows::Graphics::DirectX::Direct3D11;
}

namespace {

// Pull the raw ID3D11Texture2D backing a WinRT capture-frame surface.
winrt::com_ptr<ID3D11Texture2D> SurfaceTexture(const winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DSurface& surface) {
	auto access = surface.as<::IDirect3DDxgiInterfaceAccess>();
	winrt::com_ptr<ID3D11Texture2D> texture;
	winrt::check_hresult(access->GetInterface(winrt::guid_of<ID3D11Texture2D>(), texture.put_void()));
	return texture;
}

}

using QOverlay::Capture::Win::WgcFramePool;

WgcFramePool::~WgcFramePool() {
	Stop();
}

bool WgcFramePool::StartMonitor(HMONITOR monitor, ID3D11Device* device) {
	winrt::GraphicsCaptureItem item{ nullptr };
	try {
		auto interop = winrt::get_activation_factory<winrt::GraphicsCaptureItem, ::IGraphicsCaptureItemInterop>();
		winrt::check_hresult(interop->CreateForMonitor(
			monitor,
			winrt::guid_of<winrt::GraphicsCaptureItem>(),
			winrt::put_abi(item)));
	} catch (const winrt::hresult_error& e) {
		fmt::print("WgcFramePool: CreateForMonitor failed: {}\n", winrt::to_string(e.message()));
		return false;
	}
	return StartItem(item, device);
}

bool WgcFramePool::StartWindow(HWND window, ID3D11Device* device) {
	winrt::GraphicsCaptureItem item{ nullptr };
	try {
		auto interop = winrt::get_activation_factory<winrt::GraphicsCaptureItem, ::IGraphicsCaptureItemInterop>();
		winrt::check_hresult(interop->CreateForWindow(
			window,
			winrt::guid_of<winrt::GraphicsCaptureItem>(),
			winrt::put_abi(item)));
	} catch (const winrt::hresult_error& e) {
		fmt::print("WgcFramePool: CreateForWindow failed: {}\n", winrt::to_string(e.message()));
		return false;
	}
	return StartItem(item, device);
}

bool WgcFramePool::StartItem(const winrt::GraphicsCaptureItem& item, ID3D11Device* device) {
	if (device == nullptr) return false;

	m_device.copy_from(device);
	m_device->GetImmediateContext(m_context.put());

	// Wrap our D3D11 device as a WinRT IDirect3DDevice so WGC renders frames onto it.
	winrt::com_ptr<IDXGIDevice> dxgiDevice;
	if (FAILED(m_device->QueryInterface(winrt::guid_of<IDXGIDevice>(), dxgiDevice.put_void()))) {
		fmt::print("WgcFramePool: failed to get IDXGIDevice\n");
		return false;
	}
	winrt::com_ptr<::IInspectable> inspectable;
	if (FAILED(CreateDirect3D11DeviceFromDXGIDevice(dxgiDevice.get(), inspectable.put()))) {
		fmt::print("WgcFramePool: CreateDirect3D11DeviceFromDXGIDevice failed\n");
		return false;
	}
	m_winrtDevice = inspectable.as<winrt::IDirect3DDevice>();

	m_item = item;
	const winrt::SizeInt32 size = m_item.Size();
	RecreateFramePool(static_cast<std::uint32_t>(size.Width), static_cast<std::uint32_t>(size.Height));
	if (m_framePool == nullptr) return false;

	m_session = m_framePool.CreateCaptureSession(m_item);

	using winrt::Windows::Foundation::Metadata::ApiInformation;

	// Windows 11 draws a colored capture border by default. Drop it if the running OS
	// exposes the toggle (GraphicsCaptureSession.IsBorderRequired, added in Win11).
	try {
		if (ApiInformation::IsPropertyPresent(
				L"Windows.Graphics.Capture.GraphicsCaptureSession", L"IsBorderRequired")) {
			m_session.IsBorderRequired(false);
		}
	} catch (const winrt::hresult_error& e) {
		// Not permitted on this build/policy — capture still works, border remains.
		fmt::print("WgcFramePool: could not disable capture border: {}\n", winrt::to_string(e.message()));
	}

	m_session.StartCapture();
	fmt::print("WgcFramePool: started capture ({}x{})\n", m_width, m_height);
	return true;
}

void WgcFramePool::RecreateFramePool(std::uint32_t width, std::uint32_t height) {
	if (width == 0 || height == 0) return;

	m_width = width;
	m_height = height;

	if (!EnsureFrameTexture(width, height)) {
		m_framePool = nullptr;
		return;
	}

	const winrt::SizeInt32 size{ static_cast<std::int32_t>(width), static_cast<std::int32_t>(height) };
	if (m_framePool == nullptr) {
		// Free-threaded pool: safe to poll TryGetNextFrame from the main thread without a
		// DispatcherQueue (we never subscribe to FrameArrived).
		m_framePool = winrt::Direct3D11CaptureFramePool::CreateFreeThreaded(
			m_winrtDevice,
			winrt::DirectXPixelFormat::B8G8R8A8UIntNormalized,
			2,
			size);
	} else {
		m_framePool.Recreate(m_winrtDevice, winrt::DirectXPixelFormat::B8G8R8A8UIntNormalized, 2, size);
	}
}

bool WgcFramePool::EnsureFrameTexture(std::uint32_t width, std::uint32_t height) {
	if (m_frame != nullptr) {
		D3D11_TEXTURE2D_DESC desc{};
		m_frame->GetDesc(&desc);
		if (desc.Width == width && desc.Height == height) return true;
		m_frame = nullptr;
	}

	D3D11_TEXTURE2D_DESC desc{};
	desc.Width = width;
	desc.Height = height;
	desc.MipLevels = 1;
	desc.ArraySize = 1;
	desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
	desc.SampleDesc.Count = 1;
	desc.Usage = D3D11_USAGE_DEFAULT;
	desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
	// SteamVR shares DirectX overlay textures cross-process, so the submitted texture
	// must be created shareable.
	desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED;

	if (FAILED(m_device->CreateTexture2D(&desc, nullptr, m_frame.put()))) {
		fmt::print("WgcFramePool: failed to create {}x{} frame texture\n", width, height);
		return false;
	}
	return true;
}

bool WgcFramePool::Update() {
	if (m_framePool == nullptr) return false;

	// Every WinRT call below (TryGetNextFrame / ContentSize / SurfaceTexture's
	// check_hresult) throws winrt::hresult_error on a transient failure — device-lost, or a
	// race while the surface is resized (RecreateFramePool). This runs once per VR tick from
	// the timer callback, so an escaping exception would unwind through Qt and crash the app.
	// Contain it here: a bad frame is skipped (return false) and we retry next tick.
	try {
		// Drain the pool and keep only the newest frame — if the VR loop fell behind, we
		// don't want to submit stale frames.
		winrt::Windows::Graphics::Capture::Direct3D11CaptureFrame frame{ nullptr };
		for (;;) {
			auto next = m_framePool.TryGetNextFrame();
			if (next == nullptr) break;
			frame = next;
		}
		if (frame == nullptr) return false;

		// The desktop resolution can change under us; grow the pool/target to match.
		const winrt::SizeInt32 content = frame.ContentSize();
		const auto cw = static_cast<std::uint32_t>(content.Width);
		const auto ch = static_cast<std::uint32_t>(content.Height);
		if (cw != m_width || ch != m_height) {
			RecreateFramePool(cw, ch);
			// The recreated pool won't have this frame; pick it up next tick.
			return false;
		}

		auto texture = SurfaceTexture(frame.Surface());
		m_context->CopyResource(m_frame.get(), texture.get());
		return true;
	} catch (const winrt::hresult_error& e) {
		// Rate-limit: WGC can fail every tick during a device reset, and per-frame spam
		// would drown the log.
		if (m_updateErrors == 0 || (m_updateErrors % 100) == 0) {
			fmt::print("WgcFramePool::Update: capture failed ({}x): {}\n",
				m_updateErrors + 1, winrt::to_string(e.message()));
		}
		++m_updateErrors;
		return false;
	}
}

void WgcFramePool::Stop() {
	if (m_session != nullptr) {
		m_session.Close();
		m_session = nullptr;
	}
	if (m_framePool != nullptr) {
		m_framePool.Close();
		m_framePool = nullptr;
	}
	m_item = nullptr;
	m_frame = nullptr;
	m_winrtDevice = nullptr;
}
