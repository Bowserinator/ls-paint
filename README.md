# LS Paint
*A faithful FOSS MS paint clone.*

Built with [Slint](https://slint.dev) and C++.


## Install dependencies

### Windows (VS 2022 Preview Native Tools Command Prompt)

```bat
winget install Git.Git Kitware.CMake Ninja-build.Ninja Rustlang.Rustup LLVM.LLVM
```

### Ubuntu

```sh
sudo apt install build-essential cmake ninja-build git curl pkg-config clang libclang-dev libfontconfig-dev libfreetype-dev libxcb1-dev libxcb-shape0-dev libxcb-xfixes0-dev libxkbcommon-dev libssl-dev libpng-dev
```

### Fedora

```sh
sudo dnf install gcc-c++ cmake ninja-build git curl pkgconf-pkg-config clang clang-devel fontconfig-devel freetype-devel libxcb-devel libxkbcommon-devel openssl-devel libpng-devel
```

## Building

You will need to have the slint headers and compiler installed (whether it's prebuilt binaries or building from source, see slint repo for more info). Then simply:

```
mkdir build
cmake -B build
cmake --build build -j16
```
