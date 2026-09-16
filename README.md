# QOverlay

A VR window manager for SteamVR. Start with an empty control panel, then spawn overlays for any monitor or application window — interact with them using your VR controller as a virtual mouse, all from inside your headset.

The reusable SteamVR-overlay engine behind it (`lib/`, target `QOverlayLib`) is its own library in this same repo, so other Qt apps that just want a QML panel/scene shown in a SteamVR overlay (without the monitor/window capture parts) can depend on it too — see [Using QOverlayLib elsewhere](#using-qoverlaylib-elsewhere).

## Features

- **Monitor & window capture** — stream any display or top-level app window into VR via Windows.Graphics.Capture (WGC)
- **Virtual mouse input** — point with your controller to move the cursor; pull the trigger to click; grab to reposition overlays
- **Multi-overlay** — spawn and close overlays independently; each lives as its own SteamVR overlay
- **QML control panel** — rendered inside VR with Qt Quick; lists available surfaces and active overlays
- **Pluggable capture backends** — Windows (WGC + D3D11), Linux (X11 / wlroots / xdg portal, in progress), stub for unsupported platforms

## Platform support

| Platform | Capture | Input | Status |
|---|---|---|---|
| Windows 10 2004+ / 11 | WGC (D3D11) | SendInput | ✅ Working |
| Linux (X11) | XComposite | XTest | 🔧 Near complete, not tested |
| Linux (Wayland/wlroots) | wlr-screencopy | virtual-pointer/keyboard | 🏗 Skeleton |
| Linux (xdg portal) | ScreenCast + PipeWire | RemoteDesktop | 🏗 Skeleton |

## Requirements

### Windows

| Tool | Version |
|---|---|
| MSVC (Build Tools) | 2022 |
| Qt | 6.x (msvc2022_64) |
| vcpkg | latest |
| CMake | 3.16+ |
| Ninja | any recent |
| SteamVR | running at launch |

### Linux

| Tool | Notes |
|---|---|
| GCC / Clang | C++20 |
| Qt 6 | with `Gui`, `Qml`, `Quick`, `OpenGL` |
| vcpkg | for `fmt`, `glm`, `openvr` |
| CMake 3.16+ / Ninja | |

Sub-backend library deps (Debian/Ubuntu):

```
# X11
libx11-dev libxcomposite-dev libxtst-dev libxrandr-dev libxext-dev

# wlroots
libwayland-dev wayland-protocols

# xdg portal
libpipewire-0.3-dev   # + Qt6::DBus
```

Sub-backend library deps (Fedora/RHEL):

```
# X11
libX11-devel libXcomposite-devel libXtst-devel libXrandr-devel libXext-devel

# wlroots
wayland-devel wayland-protocols-devel

# xdg portal
pipewire-devel   # + qt6-qtbase-devel (DBus)
```

Each sub-backend is enabled automatically when its libraries are detected at configure time.

## Building

### 1. Environment variables

```
QT_ROOT    — root of your Qt 6 installation
             Windows: C:\Qt\6.11.0\msvc2022_64
             Linux (distro Qt): /usr
VCPKG_ROOT — root of your vcpkg clone, e.g. E:\vcpkg
```

On Linux, `fmt`, `glm`, and `openvr` are pulled and built by vcpkg (manifest mode)
at configure time from [`vcpkg.json`](vcpkg.json); Qt 6 comes from `QT_ROOT`.

### 2. Configure & build (Windows)

```bat
cmake --preset x64-debug
cmake --build out/build/x64-debug
```

Available presets: `x64-debug`, `x64-release`, `x86-debug`, `x86-release`.

### 3. Configure & build (Linux)

```sh
cmake --preset linux-debug
cmake --build out/build/linux-debug
```

Available presets: `linux-debug`, `linux-release`.

The configure step prints which capture sub-backends were found and enabled. This
builds both `lib/` (`QOverlayLib`) and `app/` (`QOverlay`) - there's no separate step
for the library, it's just another `add_subdirectory` in the same configure.

### 4. Run

Start SteamVR first, then launch `QOverlay.exe` (or the Linux binary). The control panel appears in VR. Use **Refresh** to enumerate available surfaces, **Add** to spawn an overlay, and **Close** to remove one.

## Architecture

Two overlay kinds run in the same process:

- **Capture overlays** (monitors / app windows) — WGC → D3D11 texture → submitted to OpenVR as `TextureType_DirectX`. No Qt involved in this path; uses a separate D3D11 device from Qt's GL context.
- **QML overlays** (control panel, virtual keyboard) — `QQuickRenderControl` → GL texture → OpenVR as `TextureType_OpenGL`.

OpenVR accepts a different texture type per overlay, so mixing GL and D3D11 overlays in one process is fine.

### Capture backend abstraction

The neutral contract lives in `app/src/capture/capture_backend.h`:

- `ICaptureBackend` — `enumerateSurfaces()`, `grabThumbnail()`, `createSource()`
- `ICaptureSource` — `submit(overlay)`, `injectMouse()`, `mouseGone()`, `frameSize()`

The backend is selected at configure time via `-DQOVERLAY_BACKEND=windows|linux|stub` (auto-detected). Each backend lives under `app/src/backends/<name>/`.

### Source layout

```
lib/                    — QOverlayLib: the reusable SteamVR-overlay engine (see below)
  src/
    vr/                 — overlay lifecycle, QML render pipeline, raycaster, grab, math
    config.*, log.*     — leftHanded/click/grab thresholds, logging facade
app/                     — QOverlay: this window-manager app, built against QOverlayLib
  src/
    vr/capture_overlay_scene.* — IOverlayScene backed by a capture surface, not QML
    capture/            — backend-neutral capture contract
    input/              — desktop_input.h (neutral) + per-platform impl
    backends/
      windows/          — WGC + D3D11 capture, SendInput injection
      linux/            — X11, wlroots, xdg portal sub-backends + GL submit helper
    window_manager.*    — QML-exposed model: surface list + active overlay list
  qml/                  — ControlPanel, ControlBar, Launcher, Keyboard
  bindings/             — SteamVR action manifest + default controller bindings
```

## Using QOverlayLib elsewhere

Another Qt app that wants to show a QML scene in a SteamVR overlay (its own control
panel, a HUD, whatever - not monitor/window capture, that's `app/`'s own job) can pull
in just `lib/` via CMake's `FetchContent`, without the rest of this repo:

```cmake
include(FetchContent)
FetchContent_Declare(qoverlaylib
    GIT_REPOSITORY https://github.com/optocloud/qoverlay.git
    GIT_TAG main
    SOURCE_SUBDIR lib
)
FetchContent_MakeAvailable(qoverlaylib)

target_link_libraries(your_app PRIVATE QOverlay::Lib)
```

Consumers need their own `bindings/<name>_actions.json` action manifest
(`Input::Initialize()` looks for `bindings/qoverlay_actions.json` next to the
executable - see `lib/src/vr/input.cpp`; making that path configurable instead of
hardcoded is a known follow-up) and to call `InitLogging("<your-app-name>")` instead of
relying on the `"qoverlay"` default log filename.

## Linux backends

See [`docs/LINUX_BACKENDS.md`](docs/LINUX_BACKENDS.md) for the full breakdown of the X11, wlroots, and xdg portal backends, their remaining TODOs, and the runtime chooser logic.

## License

GPL-3.0 — see [LICENSE](LICENSE).
