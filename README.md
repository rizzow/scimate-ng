# SCIMate NG

A portable successor to SCI Companion, starting with a headless SCI0 resource
read/write pipeline. The core uses C++17 and has no Qt or other external
dependencies. The desktop editor is future work.

## Build and test

Requires CMake 3.20+ and a C++17 compiler:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
```

To build only the core:

```sh
cmake -S . -B build-core -DSCIMATE_BUILD_CLI=OFF \
  -DSCIMATE_BUILD_APPLICATION=OFF -DBUILD_TESTING=OFF
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

## Read, export, edit, and rebuild

```sh
# Validate headers and decode all referenced resources.
./build/scimate inspect --format sci0 /path/to/game

# Create an export directory containing editable payloads and original files.
./build/scimate export --format sci0 /path/to/game /path/to/new-export

# Edit raw files in new-export/payloads/, then reconstruct into a new directory.
./build/scimate import --format sci0 /path/to/new-export /path/to/new-game
```

SCI0 compression methods 0 (stored), 1 (LZW), and 2 (Huffman) are supported.
Exports contain `manifest.tsv`, ordinal `payloads/*.bin` files, and snapshots
of the original map and referenced volumes under `original/`. Payloads are raw
decoded bytes, not standalone SCI patch files. Keep the manifest and snapshots
unchanged; modify only payload files. Duplicate resource IDs remain separate.

Unchanged imports preserve the original map and referenced volumes byte-for-byte,
including unused bytes within those volumes. Edited imports recalculate offsets,
preserve unchanged compressed records, and write changed payloads uncompressed.
The importer verifies the reconstruction and reopens the saved files. Output
directories must not exist; neither command overwrites an existing destination.

This reconstructs the packaged resources, not a complete runnable game folder:
interpreters, scripts on disk, configuration, patch files, and unreferenced
volumes are not copied. No automatic format detection, SCI0-with-SCI1-layout,
SCI1/SCI1.1 support, recompression, resource additions/deletions/renaming,
payload-format editing, previews, or GUI are implemented. A valid reconstruction
does not prove that edited payloads are meaningful to the SCI engine.

See [round-trip guarantees, limits, and export format](docs/sci0-roundtrip.md).

## Read scripts

Exports now also contain `scripts/NNNNNNN.asm` listings for script resources,
using the same ordinal as their payload file. These are human-readable
disassemblies with opcode mnemonics, branch/call labels, object names and
properties, method entry points, strings, exports, and locals. Unknown selector,
kernel, and variable names remain numeric. The CLI reports listing/error counts;
unsupported or malformed scripts get `scripts/NNNNNNN.error.txt` instead.

You can also disassemble a single decoded payload to stdout:

```sh
./build/scimate disassemble --format sci0 /path/to/export/payloads/0000000.bin
```

The input is a raw decoded script, not a standalone patch file or volume record.
These listings are **not decompiled SCI source and cannot be compiled/imported**.
Import reads the binary payloads and ignores the inspection listings. This step
does not recover original source names, comments, or high-level control flow.
See [supported script layouts and diagnostics](docs/sci0-scripts.md).

See [format and architecture notes](docs/sci0-map.md),
[source provenance](docs/provenance.md), and [validation](docs/validation.md).
Adapted code is GPL-2.0-or-later; see [COPYING](COPYING).
