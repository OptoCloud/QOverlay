cmake_minimum_required (VERSION 3.21)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

project (
	QOverlayProject
	VERSION 0.1.0
	DESCRIPTION "Open source SteamVR window manager, and the reusable overlay engine behind it"
	LANGUAGES CXX
)

include(StandardProjectSettings)
include(ConfigureQt)

# lib/ is the reusable SteamVR-overlay engine (QOverlayLib, alias QOverlay::Lib) - see
# lib/CMakeLists.txt. app/ is this repo's own consumer of it, the QOverlay window-manager
# app - see app/CMakeLists.txt. Kept in one repo (rather than lib/ living separately and
# app/ pulling it in via FetchContent) since the two evolve together during this project's
# early life; other consumers (e.g. pgvrcx's desktop/) can still depend on just lib/ via
# FetchContent + add_subdirectory(<fetched>/lib).
add_subdirectory(lib)
add_subdirectory(app)

# ── Packaging (Windows MSI via WiX v4/v5 .NET tools, through CPack) ─────────────────────────
# Only meaningful on Windows; CPack still loads harmlessly elsewhere, it just has nothing to
# package without a WiX-capable generator.
#
# Install rules are split into two CPack components:
#   Runtime      - what an end user needs: QOverlay.exe, QOverlayLib.dll, openvr_api.dll,
#                  bindings/, qml/, the .vrmanifest, the deployed Qt runtime and the MSVC CRT.
#   Development  - QOverlayLib headers, import lib, CMake export/config files (for consumers
#                  that `cmake --install` the lib instead of FetchContent'ing it).
# The MSI only ships Runtime; `cmake --install` still installs both by default.
if (WIN32)
    # MSVC CRT (msvcp140.dll, vcruntime140*.dll) next to the exe, so a packaged install runs on
    # a machine without the VC++ Redistributable. Must come before include(CPack).
    set(CMAKE_INSTALL_SYSTEM_RUNTIME_DESTINATION bin)
    set(CMAKE_INSTALL_SYSTEM_RUNTIME_COMPONENT Runtime)
    include(InstallRequiredSystemLibraries)

    set(CPACK_PACKAGE_NAME "QOverlay")
    set(CPACK_PACKAGE_VENDOR "QOverlay")
    set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "${PROJECT_DESCRIPTION}")
    set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
    set(CPACK_GENERATOR "WIX")

    # Only the Runtime component goes into the MSI. CPACK_WIX_UI_REF is forced back to the
    # plain install-dir dialog set - with components CPack otherwise defaults to the
    # feature-tree UI, which is pointless for a single feature.
    set(CPACK_COMPONENTS_ALL Runtime)
    set(CPACK_COMPONENT_RUNTIME_DISPLAY_NAME "QOverlay")
    set(CPACK_COMPONENT_RUNTIME_REQUIRED ON)
    set(CPACK_WIX_UI_REF "WixUI_InstallDir")

    # WiX license dialog needs .rtf or .txt (txt gets converted); the repo LICENSE has no
    # extension, so stage a .txt copy in the build tree.
    configure_file(${CMAKE_CURRENT_SOURCE_DIR}/LICENSE ${CMAKE_BINARY_DIR}/LICENSE.txt COPYONLY)
    set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_BINARY_DIR}/LICENSE.txt")

    # WiX v4+ (.NET tool `wix`, plus the WixToolset.UI.wixext extension) - see README.
    set(CPACK_WIX_VERSION 4)

    # Per-user install (no UAC prompt, no admin required): a single-user VR utility has no need
    # for a shared multi-user install, and unsigned per-machine installers add friction. With
    # perUser scope CPack roots the install at LocalAppDataFolder, so this lands in
    # %LocalAppData%\Programs\QOverlay, the conventional per-user programs location.
    set(CPACK_WIX_INSTALL_SCOPE perUser)
    set(CPACK_PACKAGE_INSTALL_DIRECTORY "Programs/QOverlay")
    set(CPACK_WIX_ROOT_FEATURE_TITLE "QOverlay")
    set(CPACK_WIX_PROGRAM_MENU_FOLDER "QOverlay")
    # Start Menu shortcut: "<exe name without .exe>;<shortcut label>".
    set(CPACK_PACKAGE_EXECUTABLES "QOverlay;QOverlay")

    # Pinned once, permanently - MUST NOT be regenerated on future releases. This is what
    # lets MSI recognize "newer version of the same product" and replace the old install
    # cleanly on upgrade instead of installing side-by-side or failing. See CPack WIX docs:
    # https://cmake.org/cmake/help/latest/cpack_gen/wix.html
    set(CPACK_WIX_UPGRADE_GUID "6252DC08-0E0B-4201-A859-D086F5466113")

    include(CPack)
endif ()
