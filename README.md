# SCIMate NG

A portable successor to SCI Companion, starting with a headless SCI0 resource
read/write pipeline and a Qt project-management desktop app. The core uses
C++17 and has no Qt or other external dependencies. The `scimate` CLI and
`scimate-desktop` GUI are separate executables.

## Build and test

Requires CMake 3.20+ and a C++17 compiler. The CLI/application layer also needs
libpng 1.6 and zlib; the core alone still has no external dependencies.

On macOS: `brew install libpng`. On Debian/Ubuntu: install `libpng-dev`.
On Windows: install `libpng:x64-windows` through vcpkg and pass its CMake
toolchain file when configuring. CMake does not download dependencies.

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

## Desktop projects

Project creation keeps the decoded game in memory instead of re-reading every
exported PNG. Application workflows expose optional per-phase/resource progress
callbacks; existing CLI usage and the Qt-free application API remain available.

The GUI is optional; the commands above still build the **`scimate` CLI without
Qt**. Enable the separate **`scimate-desktop`** target with Qt 6 Widgets and
Concurrent installed (Qt 6.2+; Qt Test is also needed when testing):

```sh
# macOS: brew install qtbase (a full Qt installation also works)
cmake -S . -B build-desktop -DSCIMATE_BUILD_DESKTOP=ON \
  -DCMAKE_BUILD_TYPE=Debug -DCMAKE_PREFIX_PATH=/opt/homebrew/opt/qtbase
cmake --build build-desktop --parallel
ctest --test-dir build-desktop --output-on-failure
open build-desktop/scimate-desktop.app
```

Use your Qt installation prefix on other platforms. Linux packages include
`qt6-base-dev`; run `build-desktop/scimate-desktop` there. Windows produces
`scimate-desktop.exe` (usually under `Debug/`). The macOS app currently uses
installed Qt libraries; it is not a self-contained distribution bundle.

In **CLion**, open this repository and add `-DSCIMATE_BUILD_DESKTOP=ON` and
`-DCMAKE_PREFIX_PATH=/opt/homebrew/opt/qtbase` to a CMake profile. Reload CMake,
then select the `scimate-desktop` run target. Select `scimate` to run the CLI.

- **New Project from Game** creates a named workspace in a new folder from an
  existing SCI0 game. It preserves original resources and exports editable PNGs.
- **Open Project** opens `project.scimate`. Recent projects are remembered.
  The workspace is movable and does not need the original game folder.
- Browse/filter resources, inspect script disassembly, and preview view cels
  including mirrored loops and background/priority/control planes. Modified resources are marked after refresh.
- **Open Resource Folder** opens the selected resource bundle or payload folder.
  Edit PNGs externally, then **Refresh from Disk** to validate and load changes.
- **Build Resources to New Folder** re-reads edits, reconstructs resources, and
  verifies the result. Existing destinations are refused. This builds resource
  maps/volumes, not script source or a complete runnable game installation.

Creation persists the project immediately; there are no unsaved in-app edits.
The existing CLI `import` command can rebuild a project folder too. See
[project format and workflow](docs/projects.md).

## List resources

```sh
./build/scimate list /path/to/game/resource.map
```

With a multi-configuration generator, the executable is in `build/Debug/`
(on Windows, `scimate.exe`). Paths containing spaces should be quoted.

Output is a tab-separated table of resource type name, number, volume number,
and byte offset. Numbers are decimal. Entries retain map order and duplicates.
SCI0 is the default; `--format sci0` remains optional. No volume files are
required for listing; maps referring to `resource.001` work without
assuming a `resource.000`. The CLI reads at most 16 MiB and validates the entire
map before printing a table. Exit codes: 0 success, 1 read/parse/output error,
2 usage error. `--help` describes the supported scope.

## Read, export, edit, and rebuild

```sh
# Validate headers and decode all referenced resources.
./build/scimate inspect /path/to/game

# Create an export directory containing editable payloads and original files.
./build/scimate export /path/to/game /path/to/new-export

# Edit raw files in new-export/payloads/, then reconstruct into a new directory.
./build/scimate import /path/to/new-export /path/to/new-game
```

SCI0 compression methods 0 (stored), 1 (LZW), and 2 (Huffman) are supported.
Exports contain `manifest.tsv`, ordinal `payloads/*.bin` files, and snapshots
of the original map and referenced volumes under `original/`. Payloads are raw
decoded bytes, not standalone SCI patch files. Keep the manifest and snapshots
unchanged; modify payloads or the supported editable view and picture bundles below.
Duplicate resource IDs remain separate.

Unchanged imports preserve the original map and referenced volumes byte-for-byte,
including unused bytes within those volumes. Edited imports recalculate offsets,
preserve unchanged compressed records, and write changed payloads uncompressed.
The importer verifies the reconstruction and reopens the saved files. Output
directories must not exist; neither command overwrites an existing destination.

This reconstructs the packaged resources, not a complete runnable game folder:
interpreters, scripts on disk, configuration, patch files, and unreferenced
volumes are not copied. No automatic format detection, SCI0-with-SCI1-layout,
SCI1/SCI1.1 support, recompression, resource additions/deletions/renaming,
script-source editing is implemented. A valid reconstruction
does not prove that edited payloads are meaningful to the SCI engine.

See [round-trip guarantees, limits, and export format](docs/sci0-roundtrip.md).

## Edit sprites

Game exports include `views/NNNNNNN/` bundles with indexed PNGs, `view.tsv`, and
the original view bytes. Edit source-frame PNGs in an image editor, then run
`scimate import` on the game export. Pixel colors must match the EGA palette
and transparency must be binary. Resize frames in the PNG; edit positioning
and the transparent index in `view.tsv`. Mirrored loops follow their source.

For a single decoded view:

```sh
./build/scimate view-export view.bin new-view-folder
# Edit new-view-folder/loop-0-cel-0.png, etc.
./build/scimate view-import new-view-folder edited-view.bin
```

Unchanged image imports preserve the original bytes. Conflicting edits to a
view's binary payload and PNG bundle are rejected. See [editable view workflow,
limits, and manifest rules](docs/sci0-views.md). Cursors use the asset workflow below.

## Decompile with sci-tools

High-level decompilation is handled separately by
[sluicebox/sci-tools](https://github.com/sluicebox/sci-tools). Install/build its
Snuffer CLI separately, then run it with an explicit output directory:

```sh
snuffer --decompile --annotate --sco /path/to/game /path/to/decompiled-output
```

SCIMate does not bundle or invoke Snuffer, and has no .NET dependency. Its
output is for source inspection; SCIMate does not compile/import `.sc` files.
Our experimental decompiler port has been retired. Disassembly remains built in.

## Read scripts

Exports now also contain `scripts/NNNNNNN.asm` listings for script resources,
using the same ordinal as their payload file. These are human-readable
disassemblies with opcode mnemonics, branch/call labels, object names and
properties, method entry points, strings, exports, and locals. Unknown selector,
kernel, and variable names remain numeric. The CLI reports listing/error counts;
unsupported or malformed scripts get `scripts/NNNNNNN.error.txt` instead.

You can also disassemble a single decoded payload to stdout:

```sh
./build/scimate disassemble /path/to/export/payloads/0000000.bin
```

The input is a raw decoded script, not a standalone patch file or volume record.
These listings are **not decompiled SCI source and cannot be compiled/imported**.
Import reads the binary payloads and ignores the inspection listings. This step
does not recover original source names, comments, or high-level control flow.
See [supported script layouts and diagnostics](docs/sci0-scripts.md).

See [format and architecture notes](docs/sci0-map.md),
[source provenance](docs/provenance.md), and [validation](docs/validation.md).
Adapted code is GPL-2.0-or-later; see [COPYING](COPYING).

## Explain a game

```sh
./build/scimate explain /path/to/game
./build/scimate explain /path/to/game view 42
./build/scimate explain /path/to/game script 100
```

Reports evidence for room instances, player-view candidates, sprite animations,
constant room transitions, and possible opening rooms. Facts, likely roles, and
unresolved behavior are labeled separately; no game execution or resource edits.
See [heuristics, confidence, and limitations](docs/sci0-explain.md).

clangd uses the existing compilation database in `cmake-build-debug/` via
`.clangd`. If you use another build directory, adjust `CompilationDatabase`.

## Edit backgrounds and masks

Game exports include `pictures/NNNNNNN/` bundles containing `visual.png`,
`priority.png` (sprite occlusion), and `control.png` (collision/region masks).
Edit these opaque 320×190 EGA PNGs, then run `scimate import`. Unchanged bundles
preserve the original picture bytes; edits retain the vector commands and add
verified pixel strokes. Unsupported pictures get diagnostics and retain raw payloads.

```sh
./build/scimate picture-export picture.bin new-picture-folder
./build/scimate picture-import new-picture-folder edited-picture.bin
```

See [picture editing rules and limits](docs/sci0-pictures.md).

## Experimental SCI1 migration

`scimate list --format sci1|sci11` and `inspect --format sci1|sci11` read DOS
SCI1 late / SCI1.1 directory-map containers (compression 0/1/2 only). These
are explicit format choices; SCI1.1 script/heap migration is not implemented.

```sh
./build/scimate migrate --to sci1 /path/to/source-game /path/to/migrated-sci1
./build/scimate inspect --format sci1 /path/to/migrated-sci1
```

This experimental format-based converter retains EGA assets, remaps kernel calls,
and adds file-I/O bridges for an SCI1 late candidate. Parser and SCI0 sound
adaptations are not implemented and their calls are rejected. Runtime
compatibility is **unverified**. The proof gate is real-game playback with
ScummVM reporting SCI1 late before adding VGA artwork. See the
[manual migration and proof procedure](docs/sci1-migration.md).

## Edit sounds, fonts, cursors, text, and vocabulary

Game exports include `assets/NNNNNNN/` bundles for the remaining resources.
SCI0 late sounds use editable MIDI and optional PCM WAV files; fonts and cursors
use PNGs; text and parser vocabulary use escaped text tables. Other layouts
remain binary bundles, with a diagnostic when editable conversion is unavailable.
Full-game import loads these edits and rejects conflicts with raw payload edits.

```sh
./build/scimate sound-export sound.bin new-sound-folder
./build/scimate sound-import new-sound-folder edited-sound.bin
./build/scimate resource-export vocab 0 vocab.bin new-vocab-folder
./build/scimate resource-import new-vocab-folder edited-vocab.bin
```

`font-export/import`, `cursor-export/import`, and `text-export/import` are also
available. See [supported formats, editing rules, and limits](docs/sci0-assets.md).
