# Building and testing

Requires CMake 3.20+ and a C++17 compiler. The CLI/application layer also needs
libpng 1.6 and zlib. The default build includes both `scimate` and
`scimate-desktop`, so Qt 6.2+ Widgets/Concurrent (and Qt Test for tests) is required.
The core alone still has no external dependencies.

On macOS: `brew install libpng qtbase`. On Debian/Ubuntu: install `libpng-dev`
and `qt6-base-dev`. On Windows: install Qt 6 and `libpng:x64-windows` through
vcpkg, and pass the Qt prefix and vcpkg CMake toolchain when configuring.
CMake does not download dependencies.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --config Debug
ctest --test-dir build -C Debug -E '^desktop$' --output-on-failure
```

Use a separate build directory as above. Git ignores typical CMake/CTest metadata,
compiled artifacts, and SCIMate binaries from accidental in-source builds too.
If Qt is not found automatically, supply its installation with
`-DCMAKE_PREFIX_PATH=/opt/homebrew/opt/qtbase` on Apple Silicon macOS.
For a Qt-free CLI build, explicitly set `-DSCIMATE_BUILD_DESKTOP=OFF`.

To build only the core:

```sh
cmake -S . -B build-core -DSCIMATE_BUILD_CLI=OFF \
  -DSCIMATE_BUILD_APPLICATION=OFF -DSCIMATE_BUILD_DESKTOP=OFF -DBUILD_TESTING=OFF
cmake --build build-core
```

GitHub Actions runs Release builds and CTest on macOS, Ubuntu, and Windows for
pushes and pull requests. A separate Ubuntu job builds the Qt desktop app and
runs its tests with the offscreen Qt platform. The aggregate **CI** check passes
only when every build/test job succeeds and can be required in branch protection.
See [the workflow](../.github/workflows/build.yml).

clangd looks for a compilation database in `cmake-build-debug/` via `.clangd`.
For another build directory, enable `-DCMAKE_EXPORT_COMPILE_COMMANDS=ON` with a
supporting generator and update `.clangd`'s `CompilationDatabase` path.

## Desktop launch and tests

If an existing CMake cache has desktop support disabled, configure it with
`-DSCIMATE_BUILD_DESKTOP=ON`. On Apple Silicon macOS, for example:

```sh
cmake -S . -B build -DSCIMATE_BUILD_DESKTOP=ON \
  -DCMAKE_BUILD_TYPE=Debug -DCMAKE_PREFIX_PATH=/opt/homebrew/opt/qtbase
cmake --build build --parallel
```

Launch `build/scimate-desktop` on Linux, `build/Debug/scimate-desktop.exe`
for a Windows Debug build, or the macOS application bundle:

```sh
open build/scimate-desktop.app
```

The macOS bundle requires the installed Qt libraries; it is not a standalone
distribution. Desktop tests may open windows. Run them explicitly when desired:

```sh
ctest --test-dir build -C Debug -R '^desktop$' --output-on-failure
```

For project usage, see [Desktop projects](projects.md).
