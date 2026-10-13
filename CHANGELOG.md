# Changelog

## Unreleased

### SCI0 EGA view core

- Add bounded view decoding/encoding, mirror relationships, EGA pixels,
  transparency, signed placement, and exact unchanged-byte preservation.
- Preserve reserved EGA header data and cover malformed inputs and size limits.

### Human-readable SCI0 script export

- Add portable, bounded disassembly of standard sectioned SCI0 scripts:
  opcode operands, branch/call labels, exports, class/instance properties,
  method entry points, strings, and locals.
- Include ordinal `scripts/*.asm` inspection listings in game exports. Preserve
  binary resources when disassembly fails, emit `*.error.txt` diagnostics, and
  report listing/error counts.
- Add `disassemble --format sci0 <decoded-script.bin>` for standalone inspection.
- Keep assembly listings explicitly separate from compilable SCI source: the
  binary importer ignores them. No decompiler or compiler is added here.
- Add script format, instruction-width, relative-target, malformed-input, and
  export/import-sidecar regression tests.
- Validate all 31 template scripts with zero listing errors, six suites in
  macOS Debug/Release/sanitizer builds, and three core-only suites.

### SCI0 resource read/write milestone

- Read and validate referenced volume files and SCI0 resource headers, including
  IDs, stored/decoded sizes, offsets, and missing/truncated data.
- Decode stored resources, SCI0 LZW, and Huffman with bounded reads,
  backreference/node checks, end-marker checks, and exact output lengths.
- Export raw payloads, an ordered versioned manifest, and original map/volume
  snapshots through a separate portable application library.
- Import payload edits and reconstruct SCI0 maps and volumes in new directories.
  Unchanged imports are byte-identical; edited imports retain unchanged records,
  store changed payloads uncompressed, and recalculate offsets.
- Reopen reconstructed bytes and saved files to verify round-trips. Preserve
  map order, duplicate IDs, and volume assignments; refuse existing outputs.
- Add CLI `inspect`, `export`, and `import` commands with explicit SCI0 selection.
- Add known-output codec, malformed-stream, width-boundary, multiple-volume,
  alias/duplicate, filesystem, CLI, and unchanged/edited round-trip tests.
- Validate all five suites in macOS Debug, Release, and sanitizer builds, plus
  both suites in a core-only build. The local 60-resource template round-trips
  byte-identically; a length-changing edit survives rebuild and re-export.
- Keep resource additions/deletions/renaming, recompression, payload-format
  validation, later SCI formats, and engine compatibility testing out of scope.

The sections below record the earlier map-only milestone; its limitations
describe that milestone, not the current read/write pipeline.

### Added

- First milestone: a dependency-free C++17 `scimate_core` library for reading
  original-layout SCI0 resource maps from byte buffers.
- Resource metadata: type, resource number, volume number, and byte offset,
  preserving record order and duplicate resource IDs.
- SCI0 type identifiers and names: view, pic, script, text, sound, memory,
  vocab, font, cursor, and patch. These identify entries; resource contents
  are not decoded.
- Bounded little-endian decoding and structured errors with byte offsets for
  truncated records, missing terminators, unsupported types, and trailing data.
  Terminator-only maps are accepted as empty maps.
- `scimate list --format sci0 <resource.map>` CLI with tab-separated output,
  a 16 MiB file limit, help, and documented exit codes. Listing requires no
  volume files and makes no assumption that volumes start at zero.
- CMake targets for the core, CLI, and CTest suites; the core can be built
  independently without Qt or downloaded dependencies.
- Synthetic parser and CLI tests, macOS/Linux/Windows CI configuration,
  build instructions, format notes, and GPL-2.0-or-later source provenance.
- Ignore rules for build output and local CLion project settings.

### Validation

- Native Apple Silicon macOS builds and both CTest suites passed, including
  AddressSanitizer and UndefinedBehaviorSanitizer runs.
- The core-only build, standalone public-header compilation, and strict
  compiler checks passed.
- All 60 map entries in the local SCI Companion SCI0 template matched an
  independent metadata decoder. Game assets are not included in this repository.
- Windows and Linux CI builds are configured but have not been verified.

### Current limitations

- Explicit SCI0 selection only; no format detection, SCI0-with-SCI1-layout,
  SCI1, or SCI1.1 map support.
- No volume validation, resource payload decoding or decompression, patch-file
  discovery, previews, editing, serialization, compiler, or desktop GUI.
- A successful map listing does not validate the referenced game resources.
