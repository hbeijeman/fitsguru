cmake_minimum_required(VERSION 3.21)
project(fitsguru VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# Homebrew prefixes (e.g. /opt/homebrew) aren't always on CMake's search path.
if(APPLE)
  find_program(BREW_EXECUTABLE brew)
  if(BREW_EXECUTABLE)
    execute_process(COMMAND ${BREW_EXECUTABLE} --prefix qt
                    OUTPUT_VARIABLE BREW_QT_PREFIX
                    OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    if(BREW_QT_PREFIX AND EXISTS "${BREW_QT_PREFIX}")
      list(APPEND CMAKE_PREFIX_PATH "${BREW_QT_PREFIX}")
    endif()
  endif()
endif()

if(APPLE)
  set(DEPS_INSTALL_HINT "brew install qt cfitsio pkg-config cmake ninja")
elseif(WIN32)
  set(DEPS_INSTALL_HINT
    "cfitsio: configure with -DCMAKE_TOOLCHAIN_FILE=%VCPKG_ROOT%/scripts/buildsystems/vcpkg.cmake (vcpkg.json pulls it in)\n"
    "  Qt:      install Qt 6 for msvc2022_64 and pass -DCMAKE_PREFIX_PATH=C:/Qt/<version>/msvc2022_64")
else()
  set(DEPS_INSTALL_HINT "sudo apt install qt6-base-dev libcfitsio-dev pkg-config cmake ninja-build g++")
endif()

# CFITSIO: pkg-config where available (Linux, Homebrew), otherwise a plain
# header/library search (vcpkg on Windows).
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
  pkg_check_modules(CFITSIO QUIET IMPORTED_TARGET cfitsio)
endif()
if(CFITSIO_FOUND)
  add_library(fg_cfitsio ALIAS PkgConfig::CFITSIO)
else()
  find_path(CFITSIO_INCLUDE_DIR fitsio.h PATH_SUFFIXES cfitsio)
  find_library(CFITSIO_LIBRARY NAMES cfitsio)
  if(CFITSIO_INCLUDE_DIR AND CFITSIO_LIBRARY)
    set(CFITSIO_FOUND TRUE)
    add_library(fg_cfitsio UNKNOWN IMPORTED)
    set_target_properties(fg_cfitsio PROPERTIES
      IMPORTED_LOCATION "${CFITSIO_LIBRARY}"
      INTERFACE_INCLUDE_DIRECTORIES "${CFITSIO_INCLUDE_DIR}")
  endif()
endif()
find_package(Qt6 QUIET COMPONENTS Widgets Concurrent)

set(MISSING_DEPS "")
if(NOT CFITSIO_FOUND)
  list(APPEND MISSING_DEPS cfitsio)
endif()
if(NOT Qt6_FOUND)
  list(APPEND MISSING_DEPS "Qt6 Widgets/Concurrent")
endif()
if(MISSING_DEPS)
  list(JOIN MISSING_DEPS ", " MISSING_DEPS_STR)
  message(FATAL_ERROR
    "Missing dependencies: ${MISSING_DEPS_STR}\n"
    "Install them with:\n"
    "  ${DEPS_INSTALL_HINT}\n")
endif()

# Release: strong optimization plus link-time optimization. No fast-math: the
# pipeline relies on NaN checks. FITSGURU_NATIVE tunes for this machine's CPU
# (the binary may then not run on older CPUs).
option(FITSGURU_NATIVE "Optimize for the build machine's CPU" OFF)
if(MSVC)
  add_compile_options($<$<CONFIG:Release>:/O2> $<$<CONFIG:Release>:/Ob3> $<$<CONFIG:Release>:/Oi>)
  add_compile_definitions(NOMINMAX)
  if(FITSGURU_NATIVE)
    add_compile_options(/arch:AVX2)
  endif()
else()
  add_compile_options($<$<CONFIG:Release>:-O3>)
  if(FITSGURU_NATIVE)
    add_compile_options(-march=native)
  endif()
endif()
include(CheckIPOSupported)
check_ipo_supported(RESULT FITSGURU_IPO OUTPUT FITSGURU_IPO_MSG LANGUAGES CXX)
if(FITSGURU_IPO)
  set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
endif()

# Pure C++ engine: no Qt allowed here.
add_library(fitsengine STATIC
  src/engine/fits_image.cpp
  src/engine/stretch.cpp
  src/engine/pyramid.cpp
  src/engine/preview.cpp
)
target_include_directories(fitsengine PUBLIC src/engine)
target_link_libraries(fitsengine PRIVATE fg_cfitsio)

qt_standard_project_setup()
qt_add_executable(fitsguru
  src/app/main.cpp
  src/app/image_view.cpp
  src/app/main_window.cpp
)
target_link_libraries(fitsguru PRIVATE fitsengine Qt6::Widgets Qt6::Concurrent)
target_compile_definitions(fitsguru PRIVATE FITSGURU_VERSION="${PROJECT_VERSION}")

# Icons, all rendered from resources/fitsguru.svg (regenerate with
# `cmake --build build --target icons`).
set(ICON_SIZES 16 24 32 48 64 128 256 512)
set(ICON_PNGS "")
foreach(size IN LISTS ICON_SIZES)
  list(APPEND ICON_PNGS resources/icons/fitsguru-${size}.png)
endforeach()
# Window / Dock icon at runtime, on every platform.
qt_add_resources(fitsguru icons PREFIX "/" BASE resources FILES ${ICON_PNGS})

if(WIN32)
  # Explorer/taskbar icon and version info of the .exe.
  configure_file(resources/fitsguru.rc.in ${CMAKE_CURRENT_BINARY_DIR}/fitsguru.rc @ONLY)
  target_sources(fitsguru PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/fitsguru.rc)
elseif(APPLE)
  # A proper .app bundle so Finder and the Dock show the icon.
  set_source_files_properties(resources/fitsguru.icns PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
  target_sources(fitsguru PRIVATE resources/fitsguru.icns)
  set_target_properties(fitsguru PROPERTIES
    MACOSX_BUNDLE TRUE
    MACOSX_BUNDLE_BUNDLE_NAME fitsguru
    MACOSX_BUNDLE_ICON_FILE fitsguru.icns
    MACOSX_BUNDLE_GUI_IDENTIFIER io.github.hbeijeman.fitsguru
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION})
endif()

include(GNUInstallDirs)
install(TARGETS fitsguru
  BUNDLE DESTINATION .
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
if(UNIX AND NOT APPLE)
  # Desktop integration: launcher entry plus hicolor theme icons.
  install(FILES resources/fitsguru.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
  install(FILES resources/fitsguru.svg
    DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)
  foreach(size IN LISTS ICON_SIZES)
    install(FILES resources/icons/fitsguru-${size}.png
      DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/${size}x${size}/apps
      RENAME fitsguru.png)
  endforeach()
endif()

# Icon generator; not part of the normal build.
add_executable(make_icons EXCLUDE_FROM_ALL tools/make_icons.cpp)
target_link_libraries(make_icons PRIVATE Qt6::Gui)
add_custom_target(icons
  COMMAND make_icons ${CMAKE_CURRENT_SOURCE_DIR}/resources/fitsguru.svg
                     ${CMAKE_CURRENT_SOURCE_DIR}/resources
  DEPENDS make_icons
  COMMENT "Rendering icons from resources/fitsguru.svg")

# Linux package: `cmake --build build --target package` -> fitsguru_<ver>_<arch>.deb
# Installs to /usr; the desktop entry associates .fits/.fit/.fts
# (application/fits) with fitsguru. Desktop, icon and MIME caches are
# refreshed by dpkg triggers.
if(UNIX AND NOT APPLE)
  set(CPACK_GENERATOR DEB)
  set(CPACK_PACKAGE_NAME fitsguru)
  set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Fast FITS image viewer for astronomical frames")
  set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/hbeijeman/fitsguru")
  set(CPACK_PACKAGING_INSTALL_PREFIX /usr)
  set(CPACK_STRIP_FILES ON)
  set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
  set(CPACK_DEBIAN_PACKAGE_MAINTAINER "hbeijeman")
  set(CPACK_DEBIAN_PACKAGE_SECTION science)
  set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)  # Qt, CFITSIO, libstdc++ from the binary
  set(CPACK_DEBIAN_PACKAGE_RECOMMENDS "qt6-wayland")
  include(CPack)
endif()
