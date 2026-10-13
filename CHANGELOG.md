# Changelog

## Unreleased

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
