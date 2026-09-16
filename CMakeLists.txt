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
