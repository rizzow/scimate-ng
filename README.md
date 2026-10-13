# SCIMate NG

A portable successor to SCI Companion, starting with a headless SCI0 resource
map inspector. The core uses C++17 and has no Qt or other external dependencies.
The desktop editor is future work.

## Build and test

Requires CMake 3.20+ and a C++17 compiler:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
```

To build only the core:

```sh
cmake -S . -B build-core -DSCIMATE_BUILD_CLI=OFF -DBUILD_TESTING=OFF
cmake --build build-core
```

## List resources

```sh
./build/scimate list --format sci0 /path/to/game/resource.map
```

With a multi-configuration generator, the executable is in `build/Debug/`
(on Windows, `scimate.exe`). Paths containing spaces should be quoted.

Output is a tab-separated table of resource type name, number, volume number,
and byte offset. Numbers are decimal. Entries retain map order and duplicates.
No volume files are required; maps referring to `resource.001` work without
assuming a `resource.000`. The CLI reads at most 16 MiB and validates the entire
map before printing a table. Exit codes: 0 success, 1 read/parse/output error,
2 usage error. `--help` describes the supported scope.

Only the original SCI0 map layout is supported. No automatic format detection,
SCI0-with-SCI1-layout, SCI1/SCI1.1 maps, patch-file discovery, volume validation,
decompression, previews, editing, or GUI are implemented. A successful listing
does not establish that the referenced resources exist or can be decoded.

See [format and architecture notes](docs/sci0-map.md),
[source provenance](docs/provenance.md), and [validation](docs/validation.md).
Adapted code is GPL-2.0-or-later; see [COPYING](COPYING).
