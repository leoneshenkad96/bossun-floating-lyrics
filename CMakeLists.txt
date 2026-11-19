cmake_minimum_required(VERSION 3.24)
project(BossunFloatingLyrics VERSION 1.2.0 LANGUAGES CXX)

include(GNUInstallDirs)
include(CTest)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(BOSSUN_PROJECT_URL "https://github.com/leoneshenkad96/bossun-floating-lyrics"
    CACHE STRING "Project URL or contact for the LRCLIB User-Agent")

find_package(Qt6 6.8 REQUIRED COMPONENTS Core DBus Network Quick Widgets)
find_package(LayerShellQt 6.6 REQUIRED)

add_library(bossun-core STATIC src/playback.cpp src/lyrics.cpp)
target_include_directories(bossun-core PUBLIC src)
target_link_libraries(bossun-core PUBLIC Qt6::Core Qt6::DBus Qt6::Network)
target_compile_definitions(bossun-core PUBLIC BOSSUN_VERSION="${PROJECT_VERSION}"
    PRIVATE BOSSUN_PROJECT_URL="${BOSSUN_PROJECT_URL}")

qt_add_executable(bossun-floating-lyrics src/main.cpp)
qt_add_resources(bossun-floating-lyrics overlay PREFIX "/" FILES qml/Overlay.qml)
set_source_files_properties(packaging/io.bossun.FloatingLyrics.svg PROPERTIES QT_RESOURCE_ALIAS icon.svg)
qt_add_resources(bossun-floating-lyrics icon PREFIX "/" FILES packaging/io.bossun.FloatingLyrics.svg)
target_link_libraries(bossun-floating-lyrics PRIVATE bossun-core Qt6::Quick Qt6::Widgets LayerShellQt::Interface)

install(TARGETS bossun-floating-lyrics RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES packaging/io.bossun.FloatingLyrics.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES packaging/io.bossun.FloatingLyrics.svg DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)

install(FILES LICENSE DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/bossun-floating-lyrics)

if(BUILD_TESTING)
    find_package(Qt6 REQUIRED COMPONENTS Test)
    find_program(DBUS_RUN_SESSION dbus-run-session REQUIRED)
    add_executable(bossun-tests tests/core.cpp)
    target_link_libraries(bossun-tests PRIVATE bossun-core Qt6::Test)
    add_test(NAME core COMMAND "${DBUS_RUN_SESSION}" -- $<TARGET_FILE:bossun-tests>)
    qt_add_executable(bossun-overlay-tests tests/overlay.cpp)
    qt_add_resources(bossun-overlay-tests overlay_test PREFIX "/" FILES qml/Overlay.qml)
    target_link_libraries(bossun-overlay-tests PRIVATE Qt6::Quick Qt6::Test)
    add_test(NAME overlay-drag COMMAND bossun-overlay-tests)
    set_tests_properties(overlay-drag PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")
endif()

set(CPACK_PACKAGE_NAME "bossun-floating-lyrics")
set(CPACK_PACKAGE_VENDOR "Bossun")
set(CPACK_PACKAGE_CONTACT "Bossun Floating Lyrics")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Spotify karaoke lyrics overlay for KDE Plasma Wayland")
set(CPACK_GENERATOR RPM)
set(CPACK_PACKAGING_INSTALL_PREFIX "/usr")
set(CPACK_RPM_FILE_NAME RPM-DEFAULT)
set(CPACK_RPM_PACKAGE_RELEASE_DIST ON)
set(CPACK_RPM_PACKAGE_LICENSE "MIT")
set(CPACK_RPM_USER_FILELIST "%license /usr/share/licenses/bossun-floating-lyrics/LICENSE")
list(APPEND CPACK_RPM_EXCLUDE_FROM_AUTO_FILELIST_ADDITION
    "/usr/share/applications" "/usr/share/licenses" "/usr/share/icons" "/usr/share/icons/hicolor"
    "/usr/share/icons/hicolor/scalable" "/usr/share/icons/hicolor/scalable/apps")
set(CPACK_RPM_PACKAGE_REQUIRES "layer-shell-qt >= 6.6, qt6-qtwayland, qt6-qtdeclarative, qt6-qtsvg")
if(BOSSUN_PROJECT_URL)
    set(CPACK_RPM_PACKAGE_URL "${BOSSUN_PROJECT_URL}")
endif()
include(CPack)
