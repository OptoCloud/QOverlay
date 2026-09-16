set (project QOverlay)

# lib/ (add_subdirectory'd before this file, from the top-level CMakeLists) already
# calls these find_package()s too, but lib/ and app/ are *sibling* subdirectories, not
# parent/child - a target an imported-target find module creates (like OpenVR::OpenVR,
# via a `function()`-scoped add_library() in cmake/FindOpenVR.cmake) only propagates
# down to descendant scopes, not across to a sibling. So each subdirectory that needs
# these targets has to find_package() them itself.
find_package(fmt CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)
find_package(OpenVR REQUIRED)
set (THREADS_PREFER_PTHREAD_FLAG ON)
find_package (Threads REQUIRED)

set (${project}_SOURCE
	"src/overlay_bridge.h"
	"src/overlay_controls.h"
	"src/overlay_controls.cpp"
	# Shared popup overlay (Options/Sources panels) - references OverlayControls/WindowManager,
	# so it lives here rather than in lib/ alongside the generic Overlay/QmlOverlayScene classes
	# it's built from.
	"src/vr/popup_overlay.h"
	"src/vr/popup_overlay.cpp"

	# Backend-neutral capture layer: the abstract backend contract and the pieces that
	# depend only on it. The concrete backend is selected below and is the only OS-specific
	# capture code — everything here compiles on every platform.
	"src/capture/capture_backend.h"
	"src/capture/backend_factory.h"
	"src/vr/capture_overlay_scene.h"
	"src/vr/capture_overlay_scene.cpp"
	"src/window_manager.h"
	"src/window_manager.cpp"
	# Desktop input injection: platform-neutral header, backend-specific impl added below.
	"src/input/desktop_input.h"
)

# ── Capture/input backend selection ─────────────────────────────────────────────────
# Choose the platform backend at configure time: -DQOVERLAY_BACKEND=windows|linux|stub.
# Defaults to WGC on Windows, the Linux backend group on Unix, and a no-op stub otherwise.
# The "linux" backend compiles ALL available Linux sub-backends (X11 direct, wlroots direct,
# xdg portal) and a runtime chooser picks the best one that actually works on the session —
# deliberately NOT depending on the xdg portal where a direct path exists.
if (WIN32)
	set(QOVERLAY_BACKEND "windows" CACHE STRING "Capture/input backend (windows|linux|stub)")
elseif (UNIX AND NOT APPLE)
	set(QOVERLAY_BACKEND "linux" CACHE STRING "Capture/input backend (windows|linux|stub)")
else ()
	set(QOVERLAY_BACKEND "stub" CACHE STRING "Capture/input backend (windows|linux|stub)")
endif ()
message(STATUS "QOverlay capture backend: ${QOVERLAY_BACKEND}")

# Accumulated Linux backend deps (applied after add_executable).
set(QO_LINUX_LIBS "")
set(QO_LINUX_INCLUDES "")
set(QO_LINUX_DEFS "")

if (QOVERLAY_BACKEND STREQUAL "windows")
	# Windows.Graphics.Capture (WGC) + D3D11. WGC and the C++/WinRT projection headers
	# ship with the Windows SDK, so no extra find_package. SendInput/PostMessage injection.
	list(APPEND ${project}_SOURCE
		"src/backends/windows/wgc_frame_pool.h"
		"src/backends/windows/wgc_frame_pool.cpp"
		"src/backends/windows/surface_enum.h"
		"src/backends/windows/surface_enum.cpp"
		"src/backends/windows/surface_thumbnail.h"
		"src/backends/windows/surface_thumbnail.cpp"
		"src/backends/windows/wgc_source.h"
		"src/backends/windows/wgc_source.cpp"
		"src/backends/windows/wgc_backend.h"
		"src/backends/windows/wgc_backend.cpp"
		"src/backends/windows/keyboard_layout_win.cpp"
		"src/input/desktop_input_win.cpp"
	)
elseif (QOVERLAY_BACKEND STREQUAL "linux")
	# Shared GL submit helper + the runtime chooser are always built; each sub-backend is
	# added only when its libraries are found, and gated in code via QOVERLAY_HAVE_*.
	list(APPEND ${project}_SOURCE
		"src/backends/linux/linux_gl_submit.h"
		"src/backends/linux/linux_gl_submit.cpp"
		"src/backends/linux/linux_factory.cpp"
	)

	find_package(PkgConfig REQUIRED)

	# X11 direct (no portal): XComposite capture + XTest input.
	pkg_check_modules(QO_X11 x11 xcomposite xtst xrandr xext)
	if (QO_X11_FOUND)
		list(APPEND ${project}_SOURCE
			"src/backends/linux/x11_backend.h"
			"src/backends/linux/x11_backend.cpp")
		list(APPEND QO_LINUX_LIBS ${QO_X11_LIBRARIES})
		list(APPEND QO_LINUX_INCLUDES ${QO_X11_INCLUDE_DIRS})
		list(APPEND QO_LINUX_DEFS QOVERLAY_HAVE_X11)
		message(STATUS "  X11 backend: enabled")
	else ()
		message(STATUS "  X11 backend: disabled (libX11/Xcomposite/Xtst/Xrandr not found)")
	endif ()

	# wlroots direct (no portal): wlr-screencopy + virtual-pointer/keyboard.
	pkg_check_modules(QO_WL wayland-client)
	if (QO_WL_FOUND)
		list(APPEND ${project}_SOURCE
			"src/backends/linux/wlroots_backend.h"
			"src/backends/linux/wlroots_backend.cpp")
		list(APPEND QO_LINUX_LIBS ${QO_WL_LIBRARIES})
		list(APPEND QO_LINUX_INCLUDES ${QO_WL_INCLUDE_DIRS})
		list(APPEND QO_LINUX_DEFS QOVERLAY_HAVE_WLROOTS)
		message(STATUS "  wlroots backend: enabled (protocol glue via wayland-scanner is TODO)")
	else ()
		message(STATUS "  wlroots backend: disabled (wayland-client not found)")
	endif ()

	# xdg portal + PipeWire (last resort): needs D-Bus and PipeWire.
	find_package(Qt6 QUIET COMPONENTS DBus)
	pkg_check_modules(QO_PW libpipewire-0.3)
	if (Qt6DBus_FOUND AND QO_PW_FOUND)
		list(APPEND ${project}_SOURCE
			"src/backends/linux/portal_backend.h"
			"src/backends/linux/portal_backend.cpp")
		list(APPEND QO_LINUX_LIBS Qt6::DBus ${QO_PW_LIBRARIES})
		list(APPEND QO_LINUX_INCLUDES ${QO_PW_INCLUDE_DIRS})
		list(APPEND QO_LINUX_DEFS QOVERLAY_HAVE_PORTAL)
		message(STATUS "  xdg portal backend: enabled")
	else ()
		message(STATUS "  xdg portal backend: disabled (Qt6::DBus or libpipewire-0.3 not found)")
	endif ()
else ()
	# No platform backend — the window manager just lists nothing.
	list(APPEND ${project}_SOURCE
		"src/capture/backend_factory_stub.cpp"
	)
endif ()

# Add source to this project's executable.
add_executable (${project}
	main.cpp
	${${project}_SOURCE}
	${${project}_RESOURCES}
)

# Add include directories.
target_include_directories(${project} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

# Add libraries.
target_link_libraries(${project} PRIVATE
	Threads::Threads
	${CMAKE_DL_LIBS}

	QOverlay::Lib

    Qt6::Gui
    Qt6::OpenGL
    Qt6::Qml
    Qt6::Quick

	fmt::fmt-header-only
	glm::glm
	OpenVR::OpenVR
)
if (WIN32)
  target_link_libraries(${project} PRIVATE ws2_32 winmm Iphlpapi)
endif ()

# Windows capture backend deps: D3D11 + DXGI, WindowsApp.lib for C++/WinRT activation,
# dwmapi for the DWM-cloaked/extended-frame queries, gdi32/user32 for PrintWindow / BitBlt
# thumbnails and SendInput/PostMessage injection.
if (QOVERLAY_BACKEND STREQUAL "windows")
  target_compile_definitions(${project} PRIVATE QOVERLAY_BACKEND_WINDOWS)
  target_link_libraries(${project} PRIVATE d3d11 dxgi windowsapp dwmapi gdi32 user32)
endif ()

# Linux capture backend deps (whichever sub-backends were found above).
if (QOVERLAY_BACKEND STREQUAL "linux")
  target_compile_definitions(${project} PRIVATE QOVERLAY_BACKEND_LINUX ${QO_LINUX_DEFS})
  if (QO_LINUX_INCLUDES)
    target_include_directories(${project} PRIVATE ${QO_LINUX_INCLUDES})
  endif ()
  if (QO_LINUX_LIBS)
    target_link_libraries(${project} PRIVATE ${QO_LINUX_LIBS})
  endif ()
endif ()

# Hide console window in release mode.
if (CMAKE_BUILD_TYPE STREQUAL "Release")
    set_target_properties(${project} PROPERTIES
        WIN32_EXECUTABLE TRUE
    )
endif ()

# Copy OpenVR DLL to output directory.
# OpenVR::OpenVR is an UNKNOWN IMPORTED target pointing at the .lib,
# so we find the .dll in the sibling bin/ directory (vcpkg layout: lib/ and bin/).
if (WIN32)
  get_target_property(_openvr_lib OpenVR::OpenVR IMPORTED_LOCATION)
  get_filename_component(_openvr_lib_dir "${_openvr_lib}" DIRECTORY)
  get_filename_component(_openvr_parent "${_openvr_lib_dir}" DIRECTORY)
  set(_openvr_dll "${_openvr_parent}/bin/openvr_api.dll")

  add_custom_command(TARGET ${project} POST_BUILD
	COMMAND ${CMAKE_COMMAND} -E copy_if_different
	  "${_openvr_dll}"
	  $<TARGET_FILE_DIR:${project}>
  )
endif ()

# On Linux, make the executable look next to itself for shared libs and drop the
# OpenVR .so there, so the build output is self-contained (mirrors the Windows DLL copy).
if (UNIX)
  set_target_properties(${project} PROPERTIES
	BUILD_RPATH "$ORIGIN"
	INSTALL_RPATH "$ORIGIN"
  )
  add_custom_command(TARGET ${project} POST_BUILD
	COMMAND ${CMAKE_COMMAND} -E copy_if_different
	  $<TARGET_FILE:OpenVR::OpenVR>
	  $<TARGET_FILE_DIR:${project}>
  )
endif ()

# Deploy the SteamVR action manifest, default bindings and application manifest
# next to the executable so IVRInput can load them at runtime. Also deploy the
# qml/ folder used by the QQuickRenderControl overlay prototype.
add_custom_command(TARGET ${project} POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory
	${CMAKE_CURRENT_SOURCE_DIR}/bindings
	$<TARGET_FILE_DIR:${project}>/bindings
  COMMAND ${CMAKE_COMMAND} -E copy_directory
	${CMAKE_CURRENT_SOURCE_DIR}/qml
	$<TARGET_FILE_DIR:${project}>/qml
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
	${CMAKE_CURRENT_SOURCE_DIR}/qoverlay.vrmanifest
	$<TARGET_FILE_DIR:${project}>
  COMMENT "Deploying SteamVR action manifest, bindings and QML..."
)

if (WIN32)
    # Find windeployqt
    get_target_property(QT_QMAKE_EXECUTABLE Qt6::qmake IMPORTED_LOCATION)
    get_filename_component(QT_BIN_DIR "${QT_QMAKE_EXECUTABLE}" DIRECTORY)
    set(WINDEPLOYQT_EXECUTABLE "${QT_BIN_DIR}/windeployqt.exe")

    # Derive Qt root and plugins dir from Core (release path is enough to find the tree)
    get_target_property(QT_CORE_RELEASE_DLL Qt6::Core IMPORTED_LOCATION_RELEASE)
    get_filename_component(_qt_bin_dir "${QT_CORE_RELEASE_DLL}" DIRECTORY)   # .../bin
    get_filename_component(_qt_root    "${_qt_bin_dir}" DIRECTORY)           # .../mingw_64
    set(QT_PLUGINS_DIR "${_qt_root}/plugins")

    # Decide deploy mode: prefer debug, but only if the debug platform plugin exists
    if (CMAKE_BUILD_TYPE STREQUAL "Debug")
        if (EXISTS "${QT_PLUGINS_DIR}/platforms/qwindowsd.dll")
            set(WINDEPLOYQT_MODE "--debug")
        else()
            message(WARNING "qwindowsd.dll not found. Deploying as --release to avoid windeployqt failure.")
            set(WINDEPLOYQT_MODE "--release")
        endif()
    else()
        set(WINDEPLOYQT_MODE "--release")
    endif()

    add_custom_command(TARGET ${project} POST_BUILD
            COMMAND "${WINDEPLOYQT_EXECUTABLE}"
            ${WINDEPLOYQT_MODE}
            --no-translations
            --no-opengl-sw
            "$<TARGET_FILE:${project}>"
            COMMENT "Deploying Qt runtime libraries..."
    )
endif()

# ── Keyboard tester tool (Windows) ──────────────────────────────────────────────────────────
# A small desktop GUI that loads the real qml/Keyboard.qml and drives it with the mouse, so
# the VR keyboard's layout/behaviour can be iterated without entering VR. Built beside the
# overlay exe. It reuses the Win32 keyboard-layout query (keyboard_layout_win.cpp) but nothing
# from the VR/capture stack.
if (WIN32)
    add_executable(keyboard_tester
        tools/keyboard_tester/main.cpp
        tools/keyboard_tester/tester_context.h
        src/backends/windows/keyboard_layout_win.cpp
    )
    target_include_directories(keyboard_tester PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    # Point at the source QML so "Reload QML" reflects edits with no redeploy.
    target_compile_definitions(keyboard_tester PRIVATE
        "KB_QML_FILE=\"${CMAKE_CURRENT_SOURCE_DIR}/qml/Keyboard.qml\"")
    target_link_libraries(keyboard_tester PRIVATE
        Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::Widgets Qt6::QuickWidgets
        user32
    )

    # The tester needs Widgets/QuickWidgets, which the main app's windeployqt doesn't pull in;
    # deploy the Qt runtime for it too (same output dir).
    add_custom_command(TARGET keyboard_tester POST_BUILD
        COMMAND "${WINDEPLOYQT_EXECUTABLE}"
        ${WINDEPLOYQT_MODE}
        --no-translations
        --no-opengl-sw
        "$<TARGET_FILE:keyboard_tester>"
        COMMENT "Deploying Qt runtime for the keyboard tester..."
    )
endif()
