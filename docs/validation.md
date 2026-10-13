# Validation

## Desktop projects (2026-10-13)

An isolated snapshot containing the desktop/project changes on top of the
committed picture workflow passed all 15 CTest suites on Apple Silicon macOS with Qt 6.12.0, including a clean build with
`-Wall -Wextra -Wpedantic -Werror`. The five core-only suites passed with the
application, CLI, and desktop disabled. `otool -L` confirmed that the separate
`scimate` CLI links libpng/zlib and system libraries, with no Qt dependency.

Project tests cover creation, names with quotes, invalid names/descriptors,
existing destinations, moving a workspace while making its source unavailable,
unchanged reconstruction, payload edits, and manifest corruption. The offscreen
Qt test creates/reopens a project, filters/selects resources, previews cels and all three picture planes,
edits both a PNG and a binary payload externally, refreshes modified markers,
builds and checks both edits, and verifies failed opens preserve the prior
project. The GUI is a separate macOS app bundle.

A local LSL3 project contained all 985 resource records. Project build reproduced
its resource map and four referenced volumes byte-for-byte. The unchanged
workspace was also rebuilt successfully through the retained CLI `import`
command. The offscreen desktop test opened this real project, and a captured
window was visually checked. No native interactive session, Windows/Linux GUI
run, standalone deployment bundle, or SCI-engine execution is claimed. A Linux
Qt offscreen CI job is configured but has not run here.

## Editable SCI0 EGA views

The completed workflow passed all nine suites in Release and ASan/UBSan builds
on Apple Silicon macOS. The four core-only suites passed without libpng.
Strict conversion-warning checks passed for the new core/application sources.
An isolated build of the PNG feature commit also passed all nine suites.

Read-only input from `/Users/breght/Games/LSL3` produced 383 editable view
bundles containing 5,258 PNG frames, with zero view export errors. A separate
Python PNG/RLE comparison checked every frame's dimensions, palette indices,
and transparency. Unchanged export/import preserved the resource map and all
four volumes byte-for-byte. Changing one exported cel, importing, and exporting
again changed exactly one of 985 decoded resource payloads; the other 984 were
unchanged. The changed payload matched standalone view import output.

The same export produced 178 script disassemblies and six error sidecars for
unsupported or rejected script layouts. These are assembly listings, not
reconstructed source code. The local template exported two views (19 PNGs)
without errors and retained byte-identical map/volume files after import.

Synthetic tests cover RLE, mirrors, placement, transparency, all palette colors,
empty loops, reserved headers, malformed data, PNG edits and validation,
metadata constraints, conflicting binary/image edits, original snapshot checks,
and standalone/full-game CLI workflows. Original game files were not modified;
local output remains in ignored build directories. Gameplay and remote
Windows/Linux CI were not tested.

## LSL3 local game validation

Validated `/Users/breght/Games/LSL3` read-only with explicit SCI0 selection.
Both `scimate list` and `scimate inspect` returned success:

| Check | Result |
| --- | --- |
| Map size and terminator | 5,916 bytes; final six bytes all `FF` |
| Map entries | 985 |
| Unique type/number IDs | 752 (233 additional duplicate entries retained) |
| Referenced volumes | 1: 273 entries; 2: 247; 3: 233; 4: 232 |
| Compression | 32 stored, 867 SCI0 LZW, 86 Huffman |
| Total decoded bytes, counting map entries | 3,291,932 |
| Independent Python header ID/length/bounds check | Zero errors |

All 985 resources passed the core's header checks and exact-length decoding.
This supplies real-game compressed-resource coverage beyond the synthetic
fixtures. No game files were modified or copied into the repository. This
check did not export/rebuild the game, disassemble its scripts, compare decoded
bytes against another engine, or test gameplay.

## Human-readable script export

On the same Apple Silicon macOS toolchain, all six suites passed in Debug,
Release, and AddressSanitizer/UndefinedBehaviorSanitizer builds. The three
core-only suites also passed. Strict conversion-warning checks passed for the
new script parser and application changes.

The local SCI0 template exported **31 `.asm` listings with zero disassembly
errors**. These include script 0's exported `Template` object, embedded object
names, numeric method selectors, procedure/branch labels, and strings. This
checks successful parsing and readable output, not equivalence to decompiled
source or a running interpreter.

Script fixtures check signed byte/word operands, fixed byte argument counts,
local-call bases, backward and cross-section branch targets, relative lofsa,
exported code, class methods, string escaping, locals, malformed sections and
tables, reserved opcodes, instruction-boundary validation, every truncation
point of a fixture, and 3,000 deterministic byte mutations under sanitizers.
Filesystem/CLI tests check automatic listings, error sidecars, reported counts,
standalone disassembly, and that editing an `.asm` sidecar leaves the binary
round-trip unchanged.

## Milestone 2: resource round-trips

Validated locally on Apple Silicon macOS with Apple Clang 21.0.0 and
CMake 4.4.3:

| Check | Result |
| --- | --- |
| Native Debug build and CTest | 5/5 suites passed |
| Native Release build and CTest | 5/5 suites passed |
| AddressSanitizer + UndefinedBehaviorSanitizer | 5/5 suites passed |
| Core-only build, application and CLI disabled | 2/2 core suites passed |
| Strict compiler warnings, including conversion warnings | New core/application sources passed |
| Local template unchanged export/import | Map and volume byte-identical |
| Local template length-changing edit, rebuild, and re-export | All 60 decoded payloads matched |
| Windows/Linux, SCI interpreter, Windows editor | Not run |

The five suites cover map parsing, codecs/volume reconstruction, filesystem
workflows, map-listing CLI, and round-trip CLI workflows. Checks use explicit
runtime assertions, including in Release. Codec vectors cover LZW overlapping
references, every 9/10/11/12-bit width transition, a full dictionary, resets,
invalid references, truncations, Huffman branches/escapes/leaves, invalid nodes,
terminators, exact lengths, and 4,000 deterministic malformed inputs per codec.
Round-trip fixtures cover multiple volumes (1 and 63), duplicate IDs, aliased
records, gaps/trailing bytes, compressed record preservation, empty resources,
size-changing edits, maximum stored payload size, invalid headers, missing
files, rejected metadata edits, paths containing spaces, existing-destination
refusal, and validation failures before output creation.

Local template results at the reference revision in [provenance](provenance.md):

| File | Bytes | SHA-256 before and after unchanged round-trip |
| --- | --- | --- |
| `resource.map` | 366 | `b869ea45bcb9b82cae5433fb65bdcb1fd95b831aaaac54c6ce28ea5c22765041` |
| `resource.001` | 110086 | `98c69e5c9817b8d9fd57b613089ba0bae3eb68a4c19dadc3e33e03a607b199d4` |

All 60 template resources use method 0; compressed-resource checks therefore
use synthetic known-output fixtures. No real compressed-game compatibility
claim is made. The edited template test appended a byte to one raw payload to
exercise length/offset handling; it did not establish a meaningful script edit
or a runnable game.

To reproduce a template round-trip, choose destinations that do not exist:

```sh
./build/scimate export --format sci0 \
  ../SCICompanion/SCICompanion/Files/TemplateGame/SCI0 build/template-export
./build/scimate import --format sci0 build/template-export build/template-rebuilt
cmp ../SCICompanion/SCICompanion/Files/TemplateGame/SCI0/resource.map \
  build/template-rebuilt/resource.map
cmp ../SCICompanion/SCICompanion/Files/TemplateGame/SCI0/resource.001 \
  build/template-rebuilt/resource.001
```

The template is not committed or required by CI. Every synthetic test runs
without external downloads, Qt, or the original repository.

## Milestone 1: map reader

Run locally on 2026-10-13 on Apple Silicon macOS (Darwin arm64), with
Apple Clang 21.0.0 and CMake 4.4.3:

| Check | Result |
| --- | --- |
| Native Debug configure and build | Passed |
| CTest parser and CLI suites | 2/2 passed |
| Core-only build, CLI and testing disabled | Passed |
| AddressSanitizer + UndefinedBehaviorSanitizer build and CTest | 2/2 passed |
| Local SCI0 template map comparison | All 60 rows matched |
| Windows and Linux builds | CI configured; not run locally or verified remotely |

Parser tests cover known byte sequences, all type bit patterns, minimum and
maximum field values, duplicates and ordering, a terminator-only map, every
truncation point in a valid map, absent/malformed terminators, and trailing data.
Assertions remain active in Release builds. CLI tests generate synthetic files
in the build directory and check exact tab-separated output, empty maps,
malformed/missing files, explicit format selection, usage errors, and help.
No test requires Qt, downloads, Python, or the legacy repository.

Sanitizer run:

```sh
cmake -S . -B build-sanitize -DCMAKE_BUILD_TYPE=Debug \
  '-DCMAKE_CXX_FLAGS=-fsanitize=address,undefined -fno-omit-frame-pointer' \
  '-DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address,undefined'
cmake --build build-sanitize
ctest --test-dir build-sanitize --output-on-failure
```

The additional local integration input was
`../SCICompanion/SCICompanion/Files/TemplateGame/SCI0/resource.map` at the
reference revision recorded in [provenance](provenance.md). Its 366 bytes
contain 60 records and a terminator. All CLI rows were compared to a separate
Python `struct.iter_unpack('<HI', raw[:-6])` decoder with the documented masks.
All entries refer to volume 1. First row: `script 0 1 0`; last row:
`cursor 997 1 110010`.

To inspect that local fixture again:

```sh
./build/scimate list --format sci0 \
  ../SCICompanion/SCICompanion/Files/TemplateGame/SCI0/resource.map
```

This checks map metadata only, not volume contents or Windows editor behavior.
CI uses synthetic fixtures on macOS, Ubuntu, and Windows. Platform support and
minimum OS versions remain unverified until those builds have run.
