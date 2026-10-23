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

# ── Packaging (Windows MSI via WiX v5, through CPack) ───────────────────────────────────────
# Only meaningful on Windows; CPack still loads harmlessly elsewhere, it just has nothing to
# package without a WiX-capable generator.
if (WIN32)
    set(CPACK_PACKAGE_NAME "QOverlay")
    set(CPACK_PACKAGE_VENDOR "QOverlay")
    set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "${PROJECT_DESCRIPTION}")
    set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
    set(CPACK_PACKAGE_INSTALL_DIRECTORY "QOverlay")
    set(CPACK_GENERATOR "WIX")

    # Per-user install by default (no UAC prompt, no admin required) — see
    # tmp/review2/windows-installer.md for the rationale (unsigned-build friction, no need
    # for a shared multi-user install for a single-user VR utility).
    set(CPACK_WIX_ROOT_FEATURE_TITLE "QOverlay")
    set(CPACK_WIX_PROGRAM_MENU_FOLDER "QOverlay")

    # Pinned once, permanently — MUST NOT be regenerated on future releases. This is what
    # lets MSI recognize "newer version of the same product" and replace the old install
    # cleanly on upgrade instead of installing side-by-side or failing. See CPack WIX docs:
    # https://cmake.org/cmake/help/latest/cpack_gen/wix.html
    set(CPACK_WIX_UPGRADE_GUID "6252DC08-0E0B-4201-A859-D086F5466113")

    include(CPack)
endif ()
