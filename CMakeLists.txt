cmake_minimum_required(VERSION 3.21)
project(fitsguru LANGUAGES CXX)

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
else()
  set(DEPS_INSTALL_HINT "sudo apt install qt6-base-dev libcfitsio-dev pkg-config cmake ninja-build g++")
endif()

find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
  pkg_check_modules(CFITSIO QUIET IMPORTED_TARGET cfitsio)
endif()
find_package(Qt6 QUIET COMPONENTS Widgets)

set(MISSING_DEPS "")
if(NOT PkgConfig_FOUND)
  list(APPEND MISSING_DEPS pkg-config)
endif()
if(NOT CFITSIO_FOUND)
  list(APPEND MISSING_DEPS cfitsio)
endif()
if(NOT Qt6_FOUND)
  list(APPEND MISSING_DEPS "Qt6 Widgets")
endif()
if(MISSING_DEPS)
  list(JOIN MISSING_DEPS ", " MISSING_DEPS_STR)
  message(FATAL_ERROR
    "Missing dependencies: ${MISSING_DEPS_STR}\n"
    "Install them with:\n"
    "  ${DEPS_INSTALL_HINT}\n")
endif()

# Pure C++ engine: no Qt allowed here.
add_library(fitsengine STATIC
  src/engine/fits_image.cpp
  src/engine/stretch.cpp
  src/engine/pyramid.cpp
)
target_include_directories(fitsengine PUBLIC src/engine)
target_link_libraries(fitsengine PRIVATE PkgConfig::CFITSIO)

qt_standard_project_setup()
qt_add_executable(fitsguru
  src/app/main.cpp
  src/app/image_view.cpp
  src/app/main_window.cpp
)
target_link_libraries(fitsguru PRIVATE fitsengine Qt6::Widgets)
