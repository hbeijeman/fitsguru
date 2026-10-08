# Building fitsguru

Dependencies: a C++20 compiler, CMake ≥ 3.21, Qt 6 (Widgets, Concurrent),
CFITSIO. Release is the default build type (`-O3`/`/O2` + LTO). Add
`-DFITSGURU_NATIVE=ON` to tune for the build machine's CPU.

## Linux (Ubuntu)

    sudo apt install qt6-base-dev libcfitsio-dev pkg-config cmake ninja-build g++
    cmake -S . -B build -G Ninja
    cmake --build build

Debian package (installs to /usr with a menu entry and icon, and registers
fitsguru for .fits/.fit/.fts files):

    cmake --build build --target package
    sudo apt install ./build/fitsguru_0.1_amd64.deb
    xdg-mime default fitsguru.desktop application/fits   # make it the default opener

## macOS (Homebrew)

    brew install qt cfitsio pkg-config cmake ninja
    cmake -S . -B build -G Ninja
    cmake --build build
    open build/fitsguru.app --args /path/to/file.fits   # or build/fitsguru.app/Contents/MacOS/fitsguru

## Windows (Visual Studio 2022)

1. **Qt**: install Qt 6 with the *MSVC 2022 64-bit* component, either with the
   Qt Online Installer or with aqtinstall:

       pip install aqtinstall
       aqt install-qt windows desktop 6.8.3 win64_msvc2022_64 -O C:\Qt

2. **CFITSIO**: comes from vcpkg via `vcpkg.json`. Use the vcpkg bundled with
   Visual Studio (component "vcpkg package manager"; `VCPKG_ROOT` is set in the
   Developer prompt) or a standalone clone of https://github.com/microsoft/vcpkg.

3. In the **x64 Native Tools Command Prompt for VS 2022** (it provides cl,
   cmake and ninja):

       cmake -S . -B build -G Ninja ^
         -DCMAKE_TOOLCHAIN_FILE=%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake ^
         -DCMAKE_PREFIX_PATH=C:\Qt\6.8.3\msvc2022_64
       cmake --build build

4. Copy the Qt DLLs next to the executable (vcpkg already copies cfitsio's):

       C:\Qt\6.8.3\msvc2022_64\bin\windeployqt.exe build\fitsguru.exe

## Icons

All icons are rendered from `resources/fitsguru.svg` and committed. After
editing the SVG, regenerate the PNGs, `.ico` and `.icns` with:

    cmake --build build --target icons
