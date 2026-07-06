[![DDraceNetwork](data/gui_logo.png)](https://ddnet.org)


The mighty client is a fork of the DDraceNetwork client, which is a modification of the original Teeworlds client. It enhances the gameplay experience a lot by adding important features like forcing the maodie skin to all players or showing advertisements during the game.

## Features
- **Forced maodie skin**: every single tee on the server becomes maodie. Yes, all of them.
- **Walking sounds for maodies**: hear the pitter-patter of tiny maodie feet whenever one is on the move. Requires you to drop an audio file into **data/audio/catwalking.wv** (can't ship one, because of copyright).
- **In-game advertisements**: fullscreen ad pops that appear randomly mid-run that freezes your inputs (sry i need the money, please keep them enabled).
- **Petting**: bob your gun over a nearby tee's head and a little hand appears that pets them (not perfect yet, but it works).
- **Actually decent chat translation**: foreign chat gets auto-translated and you can fire back in any language too.

## Cloning

To clone this repository:

```sh
git clone https://github.com/miightyowl/mighty-client.git
```

## Dependencies on Linux / macOS

You can install the required libraries on your system, `touch CMakeLists.txt` and CMake will use the system-wide libraries by default. You can install all required dependencies and CMake on Debian or Ubuntu like this:

```sh
sudo apt install build-essential cargo cmake git glslang-tools google-mock libavcodec-extra libavdevice-dev libavfilter-dev libavformat-dev libavutil-dev libcurl4-openssl-dev libfreetype6-dev libglew-dev libnotify-dev libogg-dev libopus-dev libopusfile-dev libpng-dev libsdl2-dev libsqlite3-dev libssl-dev libvulkan-dev libwavpack-dev libx264-dev ninja-build python3 rustc spirv-tools
```
If you distribution doesn't ship with a `rustc` that is new enough, you can use `rustup` which automatically provides `rustc` 1.85.0 and above (this command removes `rustc` and reinstalls it as part of `rustup`.):
```sh
sudo apt install rustup
```

In case the `rustc` dependency doesn't have the required version for any reason:
```sh
sudo apt install rustup-1.85
```

On older distributions like Ubuntu 18.04 don't install `google-mock`, but instead set `-DDOWNLOAD_GTEST=ON` when building to get a more recent gtest/gmock version.

On older distributions `rustc` version might be too old, to get an up-to-date Rust compiler you can use [rustup](https://rustup.rs/) with stable channel instead or try the `rustc-mozilla` package.

Or on CentOS, RedHat and AlmaLinux like this:

```sh
sudo yum install cargo cmake ffmpeg-devel freetype-devel gcc gcc-c++ git glew-devel glslang gmock-devel gtest-devel libcurl-devel libnotify-devel libogg-devel libpng-devel libx264-devel ninja-build openssl-devel opus-devel opusfile-devel python3 rust SDL2-devel spirv-tools sqlite-devel vulkan-devel wavpack-devel
```

Or on Fedora like this:

```sh
sudo dnf install cargo cmake ffmpeg-devel freetype-devel gcc gcc-c++ git glew-devel glslang gmock-devel gtest-devel libcurl-devel libnotify-devel libogg-devel libpng-devel make ninja-build openssl-devel opus-devel opusfile-devel python SDL2-devel spirv-tools sqlite-devel vulkan-devel wavpack-devel x264-devel
```

Or on Arch Linux like this:

```sh
sudo pacman -S --needed base-devel cmake curl ffmpeg freetype2 git glew glslang gmock libnotify libpng ninja opusfile python rust sdl2 spirv-tools sqlite vulkan-headers vulkan-icd-loader wavpack x264
```

Or on Gentoo like this:

```sh
emerge --ask dev-build/ninja dev-db/sqlite dev-lang/rust-bin dev-libs/glib dev-libs/openssl dev-util/glslang dev-util/spirv-headers dev-util/spirv-tools media-libs/freetype media-libs/glew media-libs/libglvnd media-libs/libogg media-libs/libpng media-libs/libsdl2 media-libs/libsdl2[vulkan] media-libs/opus media-libs/opusfile media-libs/pnglite media-libs/vulkan-loader[layers] media-sound/wavpack media-video/ffmpeg net-misc/curl x11-libs/gdk-pixbuf x11-libs/libnotify
```

Or on Void Linux like this:

```sh
sudo xbps-install -S base-devel cargo cmake ffmpeg6-devel freetype-devel git glew-devel glslang gtest-devel libcurl-devel libnotify-devel libogg-devel libpng-devel ninja openssl-devel opus-devel opusfile-devel sqlite-devel SPIRV-Tools-devel vulkan-loader wavpack-devel x264-devel SDL2-devel
```

On macOS you can use [homebrew](https://brew.sh/) to install build dependencies like this:

```sh
brew install cmake ffmpeg freetype glew glslang googletest libpng molten-vk ninja opusfile rust SDL2 spirv-tools vulkan-headers wavpack x264
```

If you don't want to use the system libraries, you can pass the `-DPREFER_BUNDLED_LIBS=ON` parameter to cmake.

DDNet requires additional libraries, some of which are bundled for the most common platforms (Windows, Mac, Linux, all x86 and x86\_64) for convenience and the official builds. The bundled libraries for official builds are now in the ddnet-libs submodule. Note that when you build and develop locally, you should ideally use your system's package manager to install the dependencies, instead of relying on ddnet-libs submodule, which does not contain all dependencies anyway (e.g. openssl, vulkan). See the previous section for how to get the dependencies. Alternatively see our [Building Guide](docs/BUILDING.md) for how to disable some features and their dependencies (for example, `-DVULKAN=OFF` won't require Vulkan).

## Building on Linux and macOS

To compile DDNet yourself, execute the following commands in the source root:

```sh
cmake -Bbuild -GNinja
cmake --build build
```

## Building on Windows with the Visual Studio IDE

Download and install some version of [Microsoft Visual Studio](https://www.visualstudio.com/) (At the time of writing, MSVS Community 2022) with **C++ support**.

You'll have to install both [Python 3](https://www.python.org/downloads/) and [Rust](https://rustup.rs/) as well.

Make sure the MSVC build tools, C++ CMake-Tools and the latest Windows SDK version appropriate to your windows version are selected in the installer.

Now open up your Project folder, Visual Studio should automatically detect and configure your project using CMake.

On your tools hotbar next to the triangular "Run" Button, you can now select what you want to start (e.g game-client or game-server) and build it.

## Building on Windows with standalone MSVC build tools

First off you will need to install the following dependencies:

- [MSVC Build Tools](https://visualstudio.microsoft.com/visual-cpp-build-tools/),
- [Python 3](https://www.python.org/downloads/windows/),
- [Rust](https://www.rust-lang.org/tools/install).

To compile with the Vulkan graphics backend (disabled by default), you also need to install the [Vulkan SDK](https://vulkan.lunarg.com/sdk/home).

To compile and build DDNet on Windows, use your IDE of choice either with a CMake integration (e.g Visual Studio Code), or by ~~**deprecated**~~ using the CMake GUI.

Configure CMake to use the MSVC Build Tools appropriate to your System by your IDE's instructions.

If you're using Visual Studio Code, you can use the [CMake Tools](https://marketplace.visualstudio.com/items?itemName=ms-vscode.cmake-tools) extension to configure and build the project.

You can then open the project folder in Visual Studio Code and press `Ctrl+Shift+P` to open the command palette, then search for `CMake: Configure`.

This will open up a prompt for you to select a kit, select your `Visual Studio` version and save it. You can now use the GUI (bottom left) to compile and build your project.