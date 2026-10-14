# Editable SCI0 views

Use the main `scimate` CLI for the entire workflow. SCI0 is the default;
explicit `--format sci0` remains accepted. Other formats are rejected.

## Whole-game workflow

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel
./build/scimate export /path/to/game /path/to/new-export
# Edit PNGs under new-export/views/NNNNNNN/ with your image editor.
./build/scimate import /path/to/new-export /path/to/new-game
./build/scimate inspect /path/to/new-game
```

The export summary reports successful view bundles and failures. Ordinals in
`views/NNNNNNN/` match rows in `manifest.tsv` and `payloads/NNNNNNN.bin`. Duplicate
resource IDs remain independent. Unsupported/malformed views retain their raw
payload and receive `views/NNNNNNN.error.txt`; they do not get editable bundles.

Import validates PNGs and metadata, encodes changed views, rebuilds volumes/map,
and reopens the saved game. Existing outputs are refused. Only resource files
are reconstructed; interpreters and other ancillary game files are not copied.

## Individual view workflow

```sh
./build/scimate view-export /path/to/decoded-view.bin /path/to/new-view-folder
# Edit frame PNGs and allowed metadata in that folder.
./build/scimate view-import /path/to/new-view-folder /path/to/new-view.bin
```

These commands consume/produce a decoded view payload, not a standalone SCI
patch file. To rebuild a game automatically, edit its exported view bundle and
use the whole-game `import` command instead of manually replacing payloads.

## Bundle and editing rules

Each bundle contains:

- `original.bin`: unmodified original payload, required for byte preservation.
- `view.tsv`: loop order, mirror relationships, frame IDs, positioning, and
  transparency. Edit only the cel-row `x`, `y`, and `transparent` columns.
- `loop-L-cel-C.png`: one editable PNG per source frame, in top-to-bottom row
  order. Mirrored loops reference a source loop and have no separate PNGs.

PNGs are indexed with the standard 16-color EGA palette and alpha zero for the
cel's transparent index. That alpha channel is the sprite's editable mask;
there is no separate SCI0 sprite mask resource. Indexed, RGB, and RGBA PNG
imports are supported (up to eight bits per channel, including packed indexed
PNGs). Nontransparent colors must match EGA exactly, alpha must be 0 or 255,
and an opaque pixel cannot use the designated transparent color. No automatic
quantization, dithering, or antialiasing is applied. Transparent RGB values are
ignored and mapped to the designated index.

PNG dimensions determine frame dimensions; resizing/cropping is supported up
to 320x200, with a minimum of 1x1. Placement is signed-byte x/y (-128..127).
Palette transparency indices range from 0 to 15. If you change the designated
transparent index, ensure no opaque pixel uses that color. Changing a source
frame automatically updates its mirrored loop's image when the engine renders
it. Mirror order/relationships and frame counts cannot be edited in this
interchange format; additions, deletions, and loop reassignment are rejected.

Do not alter `original.bin`, filenames, frame indices, or loop rows. Manifest
rows are tab-separated; LF and CRLF are accepted. Extra rows, invalid integers,
path changes, and missing files are rejected. Unknown EGA header words and loop
metadata are preserved without exposing them as editable fields.

When PNGs/metadata are unchanged, the original view bytes are preserved even
if the PNG was re-saved with different compression. Changed frames are encoded
with SCI0 EGA run-length encoding; the game pipeline stores that resource
without outer LZW/Huffman compression. The encoded view must fit SCI0's 16-bit
offsets and the game's 65,531-byte stored-payload limit. No-op game imports
remain byte-identical, including unused bytes in referenced volumes.

If a raw payload and its PNG bundle are both changed to different results,
import fails instead of choosing one. An unchanged bundle permits a binary
payload edit. Legacy binary-only exports without a `views/` directory remain
supported. Removing a bundle disables image import for that ordinal; the raw
payload is then used. PNG edits must be retained inside their bundle to import.

## Scope and implementation

`scimate_core` exposes `View`, `ViewLoop`, `ViewCel`, `read_sci0_view`, and
`write_sci0_view`, with no PNG/filesystem dependency. Limits: 50 loops,
128 cels per loop, 320x200 cels, 16 MiB total decoded source pixels, and a
65,535-byte view payload. Zero-cel loops and unused header words are preserved;
mirrors require a non-mirrored loop with the same stored offset. Runs are
bounded by output pixel count; unchanged byte layouts are preserved, while
edited output is serialized with fresh offsets and row-bounded runs.

`scimate_application` uses libpng 1.6's simplified API for PNG conversion and
owns the filesystem workflows. PNG codec errors do not enter the SCI format
core. libpng and its zlib dependency are discovered by CMake; no libraries are
downloaded by the build. See README for installation commands.

This workflow supports **SCI0 EGA views**. SCI1 VGA migration supports complete
sibling `vga/` PNG overrides with embedded palettes; see `vga-migration.md`.
Cursor masks/hotspots,
picture visual/priority/control-plane editing, sprite sheets, and an in-app
editor are not implemented. Reopening a reconstructed resource verifies its
storage/pixels/metadata, not gameplay or interpreter behavior. As with the
existing pipeline, writes are not transactional and I/O failures can leave
partial new outputs; source game files remain unchanged.
