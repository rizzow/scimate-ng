# Human-readable SCI0 script export

The core function `disassemble_sci0_script` accepts a decoded byte buffer and an
optional script number, and returns deterministic assembly-style text. It has
no filesystem, GUI, or vocabulary dependencies. Game export supplies the known
resource number; standalone CLI disassembly labels the script number unspecified.

Supported: standard sectioned SCI0 scripts, little-endian 16-bit exports, object
magic `0x1234`, SCI0 opcode encodings, and PC-relative `lofsa`/`lofss`. Instruction
operand width comes from the low opcode bit except for fixed byte argument-size
operands. Relative targets use the end of the full instruction, including the
argument byte of a local `call`, and wrap to 16 bits like SCI addresses.

Listings contain:

- Section types, addresses, and lengths.
- Instruction addresses, raw bytes, mnemonics, signed immediates, and operands.
- Labels for branch destinations and procedures found through exports, calls,
  and object method tables. These identify entry points, not reconstructed
  high-level function boundaries.
- Object/class names when embedded in the script, property values, selector IDs,
  method addresses, and export entries (including exported objects).
- Escaped strings and local/global initial values. Unknown symbolic names use
  numeric IDs; no source symbols or vocabulary names are invented.
- Raw bytes for said expressions, synonyms, relocations, and unknown sections.
  These sections are not translated into high-level source.

The parser bounds sections, words, strings, object/property/method tables, and
instruction operands. Branch/call/method targets must point to decoded
instruction boundaries; exports must point to code or an object (zero is shown
as null). Errors include a zero-based script byte offset. Padding instructions
within code sections are listed linearly; no reachability analysis is performed.

Early SCI0 scripts with a leading local-variable count, absolute-address lofsa
variants, SCI1/SCI1.1 layouts, and wide export tables are not supported. Layout
selection defaults to SCI0 (`--format sci0` is optional); there is no automatic detection.
Some alternate layouts may be structurally ambiguous, so this is not a universal
script validator. An out-of-range lofsa/lofss target is annotated as a warning.

## Export and import behavior

Export places each listing at `scripts/NNNNNNN.asm`, matching the map ordinal in
`payloads/NNNNNNN.bin`. Duplicate script IDs therefore remain distinct. If
disassembly fails, export preserves the binary payload and writes a diagnostic
to `scripts/NNNNNNN.error.txt`. The returned `ExportSummary` and CLI report both
listing and error counts. Export can succeed as a binary archive even if a
script listing fails; standalone `disassemble` returns exit code 1 on failure
without a partial listing.

The `.asm` files are inspection sidecars, not editable SCI source or an assembler
input format. Import ignores them. Editing and rebuilding scripts from readable
SCI source requires a decompiler and compiler, neither of which is implemented
by this milestone. Original variable/procedure names and comments generally
cannot be recovered from bytecode alone.

Use [sluicebox/sci-tools](https://github.com/sluicebox/sci-tools) separately for
high-level decompilation. SCIMate does not bundle it or import its source output.
