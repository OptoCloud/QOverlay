# Linux capture/input backends

> **Status:** scaffolds. Written on Windows and **not yet compiled or tested on Linux.**
> X11 is close to complete; wlroots and portal are structured skeletons with the protocol
> flow documented and the transport wiring stubbed (`TODO(linux)`).

QOverlay's capture/input is behind the `ICaptureBackend` / `ICaptureSource` contract
(`src/capture/capture_backend.h`). On Linux, one binary compiles **all** available
sub-backends and a **runtime chooser** (`linux_factory.cpp`) picks the best one that
actually works on the current session — deliberately avoiding the xdg portal where a direct
path exists.

## Why three backends

There is no single Linux capture API, and **the xdg-desktop-portal is not always present**
(e.g. Cinnamon/Mint on Wayland ships without a working ScreenCast portal — the same reason
RustDesk can't capture there). So:

| Backend | Session | Capture | Input | Portal needed |
|---|---|---|---|---|
| **X11** (`x11_backend`) | X11 (incl. XWayland apps) | XComposite + `XGetImage` (XShm upgrade TODO) | `XTest` (global) | **No** |
| **wlroots** (`wlroots_backend`) | Hyprland, Sway, river, … | `wlr-screencopy` | `virtual-pointer` + `virtual-keyboard` | **No** |
| **portal** (`portal_backend`) | GNOME, KDE Plasma (Wayland) | `ScreenCast` + PipeWire | `RemoteDesktop` | **Yes** (last resort) |

Chooser order: Wayland → wlroots (direct) → portal (only if a ScreenCast portal is really
on the bus); X11 session → X11 (direct). If nothing works, capture is reported unavailable
rather than silently broken.

## Selecting / building

```
cmake --preset linux-debug   # QOVERLAY_BACKEND defaults to "linux" on Unix
```

Each sub-backend is enabled only if its libraries are found (configure log shows which):
`QOVERLAY_HAVE_X11 / _WLROOTS / _PORTAL`.

### Dependencies (Debian/Ubuntu names)

- **X11:** `libx11-dev libxcomposite-dev libxtst-dev libxrandr-dev libxext-dev`
- **wlroots:** `libwayland-dev wayland-protocols` (+ `wayland-scanner` to generate the
  `wlr-screencopy`, `wlr-foreign-toplevel-management`, `virtual-pointer`,
  `virtual-keyboard` client glue — see below)
- **portal:** Qt 6 `DBus` module + `libpipewire-0.3-dev`, and at runtime a working
  `xdg-desktop-portal` with a ScreenCast/RemoteDesktop implementation.

## Remaining work per backend

**X11** — mostly done. TODO: XShm for zero-copy-ish reads; temporary keymap remap in
`injectText` for non-ASCII; optional damage events instead of full-frame `XGetImage`.

**wlroots** — TODO: `wl_display` connect + registry bind of the managers; run
`wayland-scanner` on the protocol XML (add generated sources to CMake); screencopy frame
loop with shm/dmabuf import; `virtual-pointer`/`virtual-keyboard` for input. Window capture
needs `hyprland-toplevel-export-v1` on Hyprland (plain wlroots may be output-only).

**portal** — TODO: the `org.freedesktop.portal.ScreenCast` D-Bus dance (CreateSession →
SelectSources → Start → PipeWire node id) via `Qt6::DBus`; PipeWire stream connect +
dmabuf/shm import; `org.freedesktop.portal.RemoteDesktop` for input. Note the target is
chosen in the compositor's own picker, so `enumerateSurfaces()` only offers
"pick a screen / pick a window".

## Notes carried over from Windows

- **Input is global** on all three (no per-window `PostMessage` equivalent), so window
  overlays behave like the monitor path — the target should be visible/raised.
- **GL submission:** each source owns a small `QOpenGLContext` (`linux_gl_submit.*`) and
  submits `TextureType_OpenGL`. Watch for a vertical flip (GL bottom-left origin) — fix via
  `SetOverlayTextureBounds` during bring-up.
- Reference implementations: **wayvr-org/wayvr** (Wayland-in-VR) and
  **elvissteinjr/DesktopPlus** (Windows, but good capture/input patterns).
