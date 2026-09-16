# Reusable SteamVR-overlay engine, shared by the QOverlay app (../app) and any other
# consumer that wants a QML panel/scene in a SteamVR overlay (e.g. pgvrcx's desktop/,
# consumed via FetchContent + add_subdirectory(<fetched>/lib)). Everything window-
# manager/capture-specific (monitor & window capture, desktop input injection, the
# control-panel QML) lives in ../app instead - this only has the generic pieces: OpenVR
# lifecycle, the Overlay/IOverlayScene/QmlOverlayScene render path, controller
# raycasting + grab-to-move input, and small math/transform/logging/config utilities.

find_package(Qt6 REQUIRED COMPONENTS Gui OpenGL Qml Quick)
find_package(fmt CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)
find_package(OpenVR REQUIRED)

add_library(QOverlayLib SHARED
    src/config.h
    src/config.cpp
    src/log.h
    src/log.cpp
    src/vr/conversion.h
    src/vr/debug.h
    src/vr/debug.cpp
    src/vr/grab_indicator.h
    src/vr/grab_indicator.cpp
    src/vr/input.h
    src/vr/input.cpp
    src/vr/ioverlay_scene.h
    src/vr/math.h
    src/vr/math.cpp
    src/vr/overlay.h
    src/vr/overlay.cpp
    src/vr/pointer_cursor.h
    src/vr/pointer_cursor.cpp
    src/vr/qml_overlay_scene.h
    src/vr/qml_overlay_scene.cpp
    src/vr/schmitt.h
    src/vr/system.h
    src/vr/system_core.cpp
    src/vr/system_input.cpp
    src/vr/tracked_device.h
    src/vr/tracked_device.cpp
    src/vr/transform.h
    src/vr/transform.cpp
)
add_library(QOverlay::Lib ALIAS QOverlayLib)

target_compile_definitions(QOverlayLib PRIVATE QOVERLAYLIB_BUILD)

target_include_directories(QOverlayLib
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
        $<INSTALL_INTERFACE:include/qoverlaylib>
)

target_link_libraries(QOverlayLib
    PUBLIC
        Qt6::Gui
        Qt6::OpenGL
        Qt6::Qml
        Qt6::Quick
        fmt::fmt-header-only
        glm::glm
        OpenVR::OpenVR
)

if (WIN32)
    target_link_libraries(QOverlayLib PRIVATE ws2_32 winmm Iphlpapi)
endif ()

# qt_standard_project_setup() (called once, at the top-level CMakeLists) sets
# CMAKE_RUNTIME_OUTPUT_DIRECTORY to the top-level CMAKE_BINARY_DIR - fine for the app's
# own .exe, but it also redirects this library's DLL there while its import .lib
# (governed by ARCHIVE_OUTPUT_DIRECTORY, untouched by that call) stays under this
# target's own binary dir - two different directories for one target, and the app's
# link step then fails to find the .lib where it expects it. Pin all three output dirs
# to this target's own binary dir explicitly so they always agree.
#
# In this repo specifically (lib/ and app/ built together, not lib/ fetched separately),
# RUNTIME_OUTPUT_DIRECTORY additionally needs to be the *shared* top-level build dir
# (matching what qt_standard_project_setup() already put QOverlay.exe in) rather than
# this target's own directory - a DLL only gets found by the OS at process start if it's
# next to the .exe (or on PATH), regardless of whether the .lib used to link against it
# lives somewhere else entirely (which is fine - the linker resolves that via the target,
# not a hardcoded path).
set_target_properties(QOverlayLib PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    # None of these classes are annotated with __declspec(dllexport)/dllimport, so on
    # MSVC the DLL exports zero symbols by default - and link.exe silently skips
    # writing an import .lib entirely when there's nothing to export, which then fails
    # the app's link step ("cannot open file ...QOverlayLib.lib"), not just missing
    # symbols. This is CMake's standard workaround: auto-export every symbol, the same
    # default Unix shared libraries already have.
    WINDOWS_EXPORT_ALL_SYMBOLS ON
)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

install(TARGETS QOverlayLib
    EXPORT QOverlayLibTargets
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
install(DIRECTORY src/
    DESTINATION include/qoverlaylib
    FILES_MATCHING PATTERN "*.h"
)
install(EXPORT QOverlayLibTargets
    FILE QOverlayLibTargets.cmake
    NAMESPACE QOverlay::
    DESTINATION lib/cmake/QOverlayLib
)

configure_package_config_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/../cmake/QOverlayLibConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/QOverlayLibConfig.cmake
    INSTALL_DESTINATION lib/cmake/QOverlayLib
)
write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/QOverlayLibConfigVersion.cmake
    VERSION 0.1.0
    COMPATIBILITY SameMajorVersion
)
install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/QOverlayLibConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/QOverlayLibConfigVersion.cmake
    DESTINATION lib/cmake/QOverlayLib
)
# FindOpenVR.cmake is a MODULE-mode find (no upstream CONFIG package for OpenVR), so
# consumers need it in their own CMAKE_MODULE_PATH too - installed alongside the
# generated config files, which add this directory to CMAKE_MODULE_PATH themselves.
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/../cmake/FindOpenVR.cmake
    DESTINATION lib/cmake/QOverlayLib
)

# Windows: OpenVR::OpenVR is an UNKNOWN IMPORTED target pointing at the .lib; the
# matching DLL lives in vcpkg's sibling bin/ dir.
if (WIN32)
    get_target_property(_openvr_lib OpenVR::OpenVR IMPORTED_LOCATION)
    get_filename_component(_openvr_lib_dir "${_openvr_lib}" DIRECTORY)
    get_filename_component(_openvr_parent "${_openvr_lib_dir}" DIRECTORY)
    set(_openvr_dll "${_openvr_parent}/bin/openvr_api.dll")

    add_custom_command(TARGET QOverlayLib POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${_openvr_dll}"
            $<TARGET_FILE_DIR:QOverlayLib>
    )
endif ()
