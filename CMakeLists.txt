cmake_minimum_required(VERSION 3.21)
project(fitsguru LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

find_package(PkgConfig REQUIRED)
pkg_check_modules(CFITSIO REQUIRED IMPORTED_TARGET cfitsio)
find_package(Qt6 REQUIRED COMPONENTS Widgets)

# Pure C++ engine: no Qt allowed here.
add_library(fitsengine STATIC
  src/engine/fits_image.cpp
  src/engine/stretch.cpp
)
target_include_directories(fitsengine PUBLIC src/engine)
target_link_libraries(fitsengine PRIVATE PkgConfig::CFITSIO)

qt_standard_project_setup()
qt_add_executable(fitsguru
  src/app/main.cpp
  src/app/image_view.cpp
)
target_link_libraries(fitsguru PRIVATE fitsengine Qt6::Widgets)
