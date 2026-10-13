# SCIMate NG

A portable successor to SCI Companion with a headless SCI0 resource
read/write pipeline and an optional Qt project-management desktop app. It also
provides explicit SCI1/SCI1.1 container inspection and experimental SCI0-to-SCI1
migration. The core uses
C++17 and has no Qt or other external dependencies. The `scimate` CLI and
`scimate-desktop` GUI are separate executables.

Editing and desktop projects currently target SCI0 EGA resources. Script
disassembly and game explanations aid inspection; script compilation and
integrated high-level decompilation are not available. Migration output remains
unverified until tested in ScummVM.

## Command-line help

Run `scimate` without arguments, or use `scimate --help`, for a grouped command
overview. Every command supports `--help` and `-h`; `scimate help <command>`
shows the same command-specific usage, examples, formats, and editing limits.

```sh
./build/scimate --help
./build/scimate help export
./build/scimate sound-import --help
./build/scimate resource-export -h
```

Use `--` before paths beginning with `--`. Invalid arguments print the selected
command's usage and a pointer to its help. Exit codes remain 0 for success,
1 for input/output or resource errors, and 2 for invalid usage.

## Build and test

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
See [the workflow](.github/workflows/build.yml).

clangd looks for a compilation database in `cmake-build-debug/` via `.clangd`.
For another build directory, enable `-DCMAKE_EXPORT_COMPILE_COMMANDS=ON` with a
supporting generator and update `.clangd`'s `CompilationDatabase` path.

## Desktop projects

Project creation keeps the decoded game in memory instead of re-reading every
exported PNG. Application workflows expose optional per-phase/resource progress
callbacks; existing CLI usage and the Qt-free application API remain available.

The normal build produces the separate **`scimate`** and **`scimate-desktop`**
executables in the same build directory. Existing CMake caches retain old options:
if `build/` was configured with desktop disabled, enable it once as follows:

```sh
# macOS: brew install qtbase (a full Qt installation also works)
cmake -S . -B build -DSCIMATE_BUILD_DESKTOP=ON \
  -DCMAKE_BUILD_TYPE=Debug -DCMAKE_PREFIX_PATH=/opt/homebrew/opt/qtbase
cmake --build build --parallel
ctest --test-dir build -E '^desktop$' --output-on-failure
# Optional GUI tests (launch Qt): ctest --test-dir build -R '^desktop$' --output-on-failure
open build/scimate-desktop.app
```

Use your Qt installation prefix on other platforms. Linux packages include
`qt6-base-dev`; run `build/scimate-desktop` there. Windows produces
`scimate-desktop.exe` (usually under `Debug/`). The macOS app currently uses
installed Qt libraries; it is not a self-contained distribution bundle.

In **CLion**, open this repository and add `-DSCIMATE_BUILD_DESKTOP=ON` and
`-DCMAKE_PREFIX_PATH=/opt/homebrew/opt/qtbase` to a CMake profile. Reload CMake,
then select the `scimate-desktop` run target. Select `scimate` to run the CLI.

- **New Project from Game** creates a named workspace in a new folder from an
  existing SCI0 game. It preserves original resources and exports editable PNGs.
- **Open Project** opens `project.scimate`. Recent projects are remembered.
  The workspace is movable and does not need the original game folder. The Open
  Project browser is resizable and remembers its layout; select `project.scimate`.
- File pickers start in your home folder on first use, then remember separate
  locations for source games, opening/creating projects, resource exports and builds.
  Missing remembered directories fall back to home. Recent projects remain in File.
- **Rename…** (also **Project → Rename Project…**) saves a new project name and
  updates the window immediately. It changes the name in `project.scimate`, not
  the folder name, and refuses to overwrite a name changed externally.
- The compact project toolbar leaves more room for resources. The viewer adapts:
  views show animation controls, pictures show plane controls, text shows strings,
  and scripts/binary resources use the full viewer height without an image area.
- **Explain → Explain / Refresh** (or **Project → Explain Project**) analyzes the
  loaded project on a background worker using the same engine as `scimate explain`.
  Expand the tree for scripts/objects, views/loops, startup, transitions and limits;
  select a branch to read its evidence. Double-click a Script/View entry to inspect
  that resource. Fact/likely/unresolved labels and diagnostics are preserved.
  Refresh from Disk first to include external edits; rerun Explain after changes.
- Filter resources by type and search text; inspect script disassembly and preview
  view cels, mirrored loops, and background/priority/control planes. **Play loop**
  animates the selected view loop. Drag either splitter to resize the browser or preview.
- **Export…** saves any selected resource as a decoded binary payload, plus PNG
  for the selected cel/plane or assembly text for scripts. PNGs retain native resolution.
- Loading shows the current phase, resource counts and elapsed time. Controls use
  the platform Qt style, including native dropdown arrows. Modified resources are
  marked after refresh.
- **Text resources** show numbered strings and support **Edit text…** with immediate
  project saves and **Export… → Numbered text strings (*.tsv)**. The editor uses
  the same lossless escaped TSV as the CLI; preserve indices referenced by scripts.
  Malformed text remains exportable as binary. External changes and conflicting
  binary/table edits are checked before saving.
- **Open Resource Folder** opens the selected resource bundle or payload folder.
  Edit PNGs externally, then **Refresh from Disk** to validate and load changes.
- The **Explain** tab and **Project → Explain Project** analyze the loaded
  project in a background worker. Expand the evidence tree for fact, likely,
  and unresolved findings; double-click a Script or View entry to inspect it
  in Resources. Use **Explain / Refresh** to rerun analysis after project edits.
- **Build Resources to New Folder** re-reads edits, reconstructs resources, and
  verifies the result. Existing destinations are refused. This builds resource
  maps/volumes, not script source or a complete runnable game installation.

Creation persists the project immediately. Text edits are saved explicitly from
the editor; Cancel discards the dialog’s changes.
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

For later containers, pass `--format sci1`, `--format sci1-ega`, or
`--format sci11` to `list` and `inspect`; formats are selected explicitly.

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
unchanged; modify payloads or the supported editable image, sound, font, cursor,
text, and vocabulary bundles described below.
Duplicate resource IDs remain separate.

Unchanged imports preserve the original map and referenced volumes byte-for-byte,
including unused bytes within those volumes. Edited imports recalculate offsets,
preserve unchanged compressed records, and write changed payloads uncompressed.
The importer verifies the reconstruction and reopens the saved files. Output
directories must not exist; neither command overwrites an existing destination.

This reconstructs the packaged resources, not a complete runnable game folder:
interpreters, scripts on disk, configuration, patch files, and unreferenced
volumes are not copied. The `export`/`import` editing workflow is SCI0 only;
later-container inspection and experimental migration are separate commands.
Automatic format detection, SCI0-with-SCI1-layout editing, recompression,
resource additions/deletions/renaming, and script-source editing are not
implemented. Changed packaged payloads are limited to 65,531 bytes.
A valid reconstruction
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

Exports also contain `scripts/NNNNNNN.asm` listings for script resources,
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

`list` and `inspect` accept `--format sci1` for DOS SCI1 late directory maps,
`--format sci11` for SCI1.1 directory maps, and `--format sci1-ega` for SCI1 EGA
packed-ID maps. SCI1 late/SCI1.1 decoding supports stored, Huffman, and MSB LZW
records (methods 0/1/2). SCI1 EGA uses SCI0-style method numbering: 0 stored,
1 MSB LZW, and 2 Huffman. Migration writes uncompressed records.

```sh
./build/scimate migrate --to sci1 /path/to/source-game /path/to/migrated-sci1
# Use the target format reported in migration-report.txt:
./build/scimate inspect --format sci1-ega /path/to/migrated-sci1
```

The source must be an SCI0 game directory, and the destination must be new.
Migration writes `resource.map`, `resource.000`, and `migration-report.txt`.
It retains EGA assets, remaps kernel calls, and bridges supported file-I/O calls.
Parser-bearing games target SCI1 EGA: word vocabulary 0 becomes 900, grammar
900 moves to 901, and optional suffix vocabulary 901 moves to 902. Other games
target SCI1 late; inspect those with `--format sci1`.

A limited ScummVM sound compatibility profile retains decoded SCI0 late sound
resources after validating sound-call frames and vocabulary 997's dispatch
condition. Unsupported or unproven calls, early sound layouts, and DoAvoider
are rejected. This does not establish compatibility with Sierra's SCI1 drivers.

The core includes structural SCI0-to-SCI1.1 script/heap splitting and validation,
but the CLI does not expose complete SCI1.1 game migration. VGA conversion is
not implemented. Runtime compatibility is **unverified**: successful container
round-trips do not prove gameplay. The proof gate is real-game playback in
unmodified ScummVM reporting the intended SCI1 generation, including parser,
sound, room transitions, and save/restore, before adding VGA artwork. See the
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
