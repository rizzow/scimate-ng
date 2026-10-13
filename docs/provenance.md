# Source provenance and licensing

Reference: local `../SCICompanion`, revision
`f1a603b48b1aa7abf94f78574a8f69a653e2ca62`.
The original repository is used read-only.

The SCI0 parsing implementation in `core/src/resource_map.cpp` adapts format
and navigation behavior from the following Philip Fortier sources, which
declare copyright (c) 2015 and GPL version 2 or later:

- `SCICompanionLib/Src/Resources/ResourceBlob.h`: `LAYOUT_SCI0`,
  `RESOURCEMAPENTRY_SCI0_BASE`, and all-ones terminator documentation.
- `SCICompanionLib/Src/Resources/ResourceContainer.h`: `SCI0MapNavigator`,
  sequential records and six-byte terminator emission.
- `SCICompanionLib/Src/Util/sci.h`: resource type numbers and names.

Also inspected `ResourceSources.h` for the map/volume boundary and
`ResourceMapOperations.cpp` for explicit format dispatch (both in the same
Resources directory). No Windows I/O, packed structs, or UI code were imported.
Native bitfields were replaced by bounded
byte reads, fixed-width integers, shifts and masks. Strict malformed-input
checks and the new CLI/tests are additions.

Milestone 2 extends this adaptation:

- `core/src/resource_game.cpp` uses `RESOURCEHEADER_SCI0` from `ResourceBlob.h`
  for the packed ID and stored-length-plus-four convention. The original
  Philip Fortier copyright/license notice is retained.
- `core/src/codecs.cpp` adapts SCI0 `decompressLZW`, `getc2`, and
  `decompressHuffman` from `SCICompanionLib/Src/Util/Codec.cpp` at the same
  revision. That file credits the FreeSCI algorithms, Carl Muckenhoupt's SCI
  decoder/sde.c, and Sergey Lapin's port. These credits are retained. Its SCI1,
  DCL, STAC, application logging, and Windows-specific code are not imported.
- `ResourceBlob.cpp::VersionAndCompressionNumberToAlgorithm` supplies the
  SCI0 method mapping (1 LZW, 2 Huffman). The bounded bit reader, validation,
  application interchange format, and reconstruction verification are new.

The codec adaptation replaces unchecked bit-buffer accesses and legacy output
overflow handling with bounded operations and explicit errors. It preserves
SCI0's LZW width-change timing and Huffman relative node/escape conventions.

The script disassembler in `core/src/script.cpp` adapts the opcode names and
SCI0 operand-width table from `Compile/PMachine.h` and `Compile/PMachine.cpp`,
section/object layout and relative-target behavior from `Compile/CompiledScript.cpp`,
and instruction interpretation from `Compile/Disassembler.cpp` (all paths under
`SCICompanionLib/Src`, same revision). These files carry Philip Fortier's 2015
GPL-2.0-or-later notice, retained in the adapted file. The new bounded parser and
formatter do not import the decompiler, compiler, application globals, packed
native reads, source formatter, or Windows dependencies.

The EGA view implementation in `core/src/view.cpp` adapts `ReadCelFrom`,
`ReadLoopFrom`, `ViewReadFromVersioned`, `WriteCelTo`, `WriteLoopTo`, and the EGA
branches of `ReadImageDataWorker`/`WriteImageData` from
`SCICompanionLib/Src/Resources/View.cpp`, at the same reference revision. The
Philip Fortier GPL-2.0-or-later notice is retained. New code uses bounded reads,
top-down palette-index buffers, explicit signed offsets, and portable models;
it does not import GDI/DIB storage, global application state, or Windows APIs.
The EGA header word used as a VGA palette pointer is preserved as reserved data
when the palette flag is absent, matching the reference's dispatch behavior.

PNG conversion uses the externally installed libpng library (PNG Reference
Library/libpng license) and its zlib dependency (zlib license). Neither library
is vendored or relicensed by this project. CMake discovers them only for the
application layer. SCI view parsing/encoding remains dependency-free.

The original copyright/license notice is retained in the adapted source.
`COPYING` is copied verbatim from `SCICompanion/Files/COPYING2`. New source files
use SPDX GPL-2.0-or-later identifiers for compatibility with this adaptation;
this does not assign a license to future third-party imports or game assets.

Committed fixtures are synthetic byte sequences in the tests. The local SCI0
template game is an additional validation input only; none of its assets are
copied into this repository. No behavioral comparison against the running
Windows editor has been performed.

The SCI0 picture renderer in `core/src/picture.cpp` adapts Sierra line rounding,
pen circle/texture tables and pen clipping from
`SCICompanionLib/Src/Resources/PicCommands.cpp` at revision
`f1a603b48b1aa7abf94f78574a8f69a653e2ca62` (Philip Fortier, 2015, GPL-2.0-or-later;
that file credits SCI Studio 3 by Brian Provinciano). The notice is retained.
Opcode/coordinate and fill semantics were cross-checked against
[ScummVM's picture renderer](https://github.com/scummvm/scummvm/blob/master/engines/sci/graphics/picture.cpp).
The bounded parser, three-plane model, PNG bundles and verified stroke-based
edit serialization are new code. No Windows drawing/UI dependencies are imported.
