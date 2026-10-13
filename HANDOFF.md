# SCIMate NG — project handoff

Date: 2026-10-13

## Script inspection implementation update

Exports now include human-readable SCI0 disassembly under `scripts/*.asm`, with
explicit diagnostic sidecars on unsupported/malformed scripts. All 31 scripts
in the local template produced listings. `scimate disassemble --format sci0`
also reads standalone decoded payloads. This is disassembly, not decompiled
SCI source; import ignores the listings and still consumes binary payloads.
No compiler/decompiler has been ported. See `docs/sci0-scripts.md`.

## Milestone 2 implementation update

SCI0 volume reading, bounded LZW/Huffman decoding, export/import, and verified
reconstruction are now implemented. The core remains filesystem/UI independent;
`scimate_application` owns file I/O and the ordinal payload/manifest interchange
format. Unchanged imports preserve map/referenced volumes byte-for-byte; edited
imports recalculate offsets and store changed payloads uncompressed. Output
directories must be new. See `docs/sci0-roundtrip.md`, `README.md`, and
`docs/validation.md` for guarantees, limits, and checks. The template resources
round-trip, but edited-game behavior in an SCI interpreter is not verified.

## Milestone 1 implementation update — 2026-10-13

The first milestone is now implemented: a dependency-free C++17 core, strict
SCI0 map reader, `scimate list --format sci0` CLI, CMake/CTest project, synthetic
parser/CLI tests, and macOS/Linux/Windows CI configuration. Native macOS Debug,
core-only, and sanitizer builds passed; all 60 entries in the local SCI0
template map matched independent decoding. Windows/Linux CI has not yet run.
See `README.md` and `docs/validation.md` for use, exact scope, and evidence.
No GUI, volume decoding, or editing has been implemented. The remaining
sections below preserve the original pre-implementation handoff context.

## Task and intent

Build a modern successor to SCI Companion, the IDE for Sierra SCI games
(SCI0 through SCI1.1). The original request was to analyze SCI Companion and
make it buildable on macOS. Inspection showed that a native desktop port
requires separating its engine code from Windows-specific UI and services.

The user then proposed a portable core and a fresh GUI, implemented gradually,
and asked whether to target multiple platforms and use SDL or Qt. The user
requested all new implementation start in:

`/Users/breght/Development/brightspark/scimate-ng`

The latest request was to write this handoff and start implementation in a new
Codex session in that directory. Do not interpret this document as evidence
that any port or build has already been completed.

## Current state

- The new directory already contains an initialized Git repository.
- No implementation, CMake project, GUI, or tests have been created yet.
- No dependencies have been installed for this project.
- Preliminary edits made during investigation in the original repository were
  removed; its working tree was verified clean before this handoff.
- The local host is an Apple Silicon Mac. Apple Clang, Xcode's macOS SDK,
  CMake, and Homebrew were found. Qt availability has not been checked.
- macOS compilation, Windows/Linux compilation, and packaging remain unverified.

## Reference implementation

Local repository:

`/Users/breght/Development/brightspark/SCICompanion`

Inspected revision:

`f1a603b48b1aa7abf94f78574a8f69a653e2ca62`

Treat it as the reference implementation. Put new work in `scimate-ng`.
Preserve working algorithms and format knowledge rather than rewriting
everything. Keep a record of source paths and revisions when moving code.

The original project uses Visual Studio projects, the `v140_xp` toolset, MFC,
Win32, GDI/GDI+, and Prof-UIS 2.92. There is no existing CMake build.
`SCICompanion` is a thin application wrapper; most implementation lives in
`SCICompanionLib/Src`. Despite its name, that library contains substantial UI
code and is not a portable engine library.

Useful reference areas:

| Area | Path relative to the reference repository |
| --- | --- |
| Resource formats, loading, saving, and rendering | `SCICompanionLib/Src/Resources/` |
| Compiler and decompiler | `SCICompanionLib/Src/Compile/` |
| Streams, compression, application state | `SCICompanionLib/Src/Util/` |
| CRC implementation | `SCICompanionLib/Src/CRC32/` |
| Existing documents and undo | `SCICompanionLib/Src/MFCDocuments/` |
| Existing editor views | `SCICompanionLib/Src/MFCViews/` |
| Existing tests | `UnitTests/` |
| SCI0 example game | `SCICompanion/Files/TemplateGame/SCI0/` |
| SCI1.1 example game | `SCICompanion/Files/TemplateGame/SCI1.1/` |

## Architectural direction discussed

The following is the recommended starting architecture from the discussion.
The user explicitly chose the new directory and deferred implementation to a
new session. Framework installation and detailed implementation choices have
not yet happened.

### Core first, in small usable increments

Start with the portable core required for a concrete resource-inspection
workflow. Do not attempt to port all of `SCICompanionLib` before building the
first UI. Once opening and listing resources works headlessly, add a small
resource-browser GUI and extend both together, one workflow at a time.

The CLI is a way to exercise the core and automate validation; the long-term
goal is a desktop editor, not merely a CLI replacement.

### One new repository with separate build targets

Keep the portable core, application layer, CLI, future GUI, and tests together
in `scimate-ng`. Use separate CMake targets and enforce dependency direction.
This permits atomic changes across layers without creating a separately
versioned library or coordinating multiple repositories prematurely.

The new project is separate from the legacy reference repository. Splitting
the core out of `scimate-ng` can be reconsidered if it develops independent
consumers and a stable public API.

Suggested layout, not yet created:

```text
CMakeLists.txt
core/          # SCI formats, models, codecs, compiler, rendering
application/   # Game sessions, document operations, workflows
apps/cli/      # Headless inspection and development tools
apps/desktop/  # Qt frontend, introduced after initial core work
tests/         # Unit, fixture, round-trip, and malformed-input tests
docs/          # Architecture and supported-format notes
```

Keep the initial layout lean; create components when they contain real code.
The core must build and test without Qt being installed.

### Cross-platform design, macOS first

Target macOS, Windows, and Linux. Use macOS as the first development and
validation host. Introduce build/test CI for all three early, without claiming
platform support before those builds actually pass.

Use standard C++ and CMake. The exact C++ standard and minimum supported OS
versions remain to be chosen. Cross-platform packaging and native integration
still require platform-specific work.

### Recommended GUI: Qt 6 Widgets

Qt Widgets is the recommended fit for the resource trees, tables, property
panels, tabs, docking, menus, shortcuts, and custom pixel editors of an IDE.
Use Qt models as adapters over core data. Qt's undo infrastructure can present
editing operations whose semantics remain independent of the frontend.

SDL 3 was considered. It supplies lower-level graphics, input, and audio, but
would leave much of the editor's desktop UI infrastructure to assemble. There
is no identified need to combine SDL and Qt initially.

Widgets was preferred over QML for the initial dense desktop interface and
direct integration with the existing C++ algorithms. A macOS-only frontend
would create additional frontend maintenance if Windows/Linux are supported.

### Layer boundaries

| Layer | Owns |
| --- | --- |
| Core, standard C++ | Binary parsing/serialization, compression, SCI resource models, palette/pixel data, SCI rendering semantics, compiler/decompiler, validation |
| Application, standard C++ | Game sessions, document state, editing operations, save workflows, background task coordination, structured diagnostics |
| Qt frontend | Windows, tabs, docks, menus, shortcuts, canvas interaction, dialogs, progress and diagnostic presentation |
| CLI | Headless access to the same core/application operations |

Dependencies point inward. Public core interfaces should not expose MFC,
Win32 handles, Qt objects, or Qt strings. Avoid global application state and
implicit UI calls in engine operations. Introduce explicit dependencies at
real platform/service boundaries rather than inventing a generic framework.

Concrete rendering boundary: the core renders SCI data into pixel buffers,
including SCI-specific priority/control planes and palette behavior. The Qt
frontend displays those buffers and owns zoom, selection handles, and input.

## Portability findings to address

- `SCICompanionLib/stdafx.h` imports MFC, Win32, GDI+, Prof-UIS, and shared
  project headers. Many translation units rely on those transitive includes.
  Extracted files need explicit, self-contained includes.
- `Resources/ResourceBlob.h` exposes `HANDLE`, `HRESULT`, and Windows string
  types in loading/saving interfaces.
- `Util/Stream.h` owns Windows file handles and file mappings.
- `Resources/RasterOperations.h` returns `HBITMAP`; rendering headers also
  expose `CDC`, `CRect`, `CPoint`, and `COLORREF`.
- Palette data uses `RGBQUAD`; portable color structs need explicit channel
  semantics and deliberate conversion at UI boundaries.
- `Util/AppState.h` mixes resource management, editor windows, logging,
  graphics initialization, and application services in a global `appState`.
  Even decompression code depends on it for logging.
- `MFCDocuments/UndoResource.h` ties resource snapshots to MFC document
  inheritance. Extract editing semantics separately from UI integration.
- `CRC32/crc.h` defines CRC32 using `unsigned long`, which is 64 bits on
  64-bit macOS but 32 bits on Windows. Use fixed-width integer types and
  verify against known vectors.
- `Util/Codec.h` uses `std::exception("message")`, a nonportable constructor;
  use an appropriate standard exception/error type.
- Codec code includes MSVC-specific integer types and unaligned access
  constructs. Decode byte order explicitly rather than casting byte buffers
  to packed native structures.
- Initial codec inspection found unchecked reads and legacy bit-buffer
  assumptions. A successful compilation alone is insufficient evidence of
  correct or safe resource decoding. Validate truncation, lengths, offsets,
  backreferences, and output bounds as code is extracted.

## Proposed first implementation milestone

Deliver a headless SCI0 resource-map reader and a CLI that lists resource
metadata, with a native macOS CMake build and meaningful tests.

1. Set up a small CMake project with an independently buildable core, CLI,
   and CTest integration. Avoid downloading dependencies unnecessarily.
2. Introduce bounded binary reading, explicit little-endian decoding, and
   portable resource identifiers/metadata.
3. Extract or adapt the SCI0 map-reading behavior from the reference code.
   Inspect `Resources/ResourceContainer.h`, `ResourceSources.h`, and
   `ResourceMapOperations.cpp`; do not assume all SCI map variants share
   the same layout.
4. Add a CLI with explicit SCI0 format selection initially. List resource
   type, number, volume, and offset. Keep map listing distinct from full
   volume validation or decompression and state unsupported features.
5. Test valid records, terminators, empty/truncated input, and malformed
   records. Use synthetic fixtures for committed tests; use the local
   template game for an additional integration check.
6. Build and run on macOS. Add CI configuration for Windows/Linux and report
   clearly which checks were actually run.

The SCI0 template contains `resource.map` and `resource.001`; do not hardcode
the assumption that every game starts with `resource.000`.

After this, add a small Qt resource browser backed by the same core. Expand
into previews, then a complete text-resource editing workflow with undo,
save-to-copy, and reopen verification. Follow with views, palettes, fonts,
pictures, sound, and script workflows according to user priorities. Track
read/edit/write support separately for each SCI version and resource type.

## Validation and provenance

Preserve existing algorithm behavior with known-output fixtures, round-trip
tests, and comparisons against the original Windows editor when available.
Do not claim Windows behavioral comparison has already been performed.
Keep the legacy editor usable as a reference during migration.

Retain original copyright and license notices in extracted source. Many
inspected SCI Companion files declare GPL-2.0-or-later; the repository also
contains third-party code with its own notices. GPL texts are present at
`SCICompanion/Files/COPYING2` and `SCICompanion/Files/COPYING`. Check the actual
files being imported and record their provenance; do not assume every bundled
dependency or template-game asset has the same license. No final license
declaration for this new project has been made in this handoff.

## Framework references reviewed

- [Qt Widgets](https://doc.qt.io/qt-6/qtwidgets-index.html)
- [Qt model/view programming](https://doc.qt.io/qt-6/model-view-programming.html)
- [Qt undo stack](https://doc.qt.io/qt-6/qundostack.html)
- [Qt supported platforms](https://doc.qt.io/qt-6/supported-platforms.html)
- [SDL 3 overview](https://wiki.libsdl.org/SDL3/FrontPage)

## Suggested opening prompt for the next session

> Read HANDOFF.md. Start the core-first implementation in this repository:
> create the CMake project, a portable SCI0 resource-map reader, a CLI to list
> resources, and tests. Keep the core independent of Qt and preserve the
> original SCICompanion repository as a reference. Build and test on this Mac,
> and report the supported scope and remaining work accurately.
