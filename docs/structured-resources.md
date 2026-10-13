# Structured resource editing

`resource-export` and game export write `resource.json` and a companion
`resource.schema.json` for resources without a standard media/table format.
JSON provides readable named records, Unicode strings, integer values, stable
IDs, and widely supported machine tooling. Two-space indentation and deterministic
key ordering keep diffs readable. The generated schema uses JSON Schema 2020-12
for editor completion and field/range validation; import additionally checks
identities, encoded text, and structural constraints.

Keep `asset.tsv`, `original.bin`, and the schema unchanged. Edit `resource.json`,
then use `resource-import` or whole-game `import`. Unchanged imports return the
original bytes, including padding and unused bits. Old binary bundles remain
importable. Binary and bundle edits must agree when both are changed.

| Layout | Editable content |
| --- | --- |
| Sectioned SCI0 scripts | Opcode mnemonics, operands, address labels, properties, methods, exports, locals, strings, synonyms and Said expressions |
| Vocabulary 995/997/998/999 | Names indexed by engine ID, including absent names |
| Vocabulary 996 | Class table reserved fields and script numbers |
| Vocabulary 994 | Property offset values |
| Vocabulary 900/910 | Grammar rule IDs and symbol arrays |
| Vocabulary 901/911 | Suffix rules, result classes and class masks |
| Vocabulary 913 | Alternate input/replacement pairs |
| AdLib patch 3 | Operator envelopes, levels, frequency, waveform, feedback and modulation flags |
| MT-32 patch 0/1 | Program patches, display strings, timbre names, common/partial parameters, optional rhythm data |
| GM patch 1/4 | Program/percussion mappings, key shifts, volume, velocity maps and initialization MIDI parameters |
| Memory or unknown layouts | Explicitly labelled byte records |

Script editing preserves section sizes, instruction widths, string byte lengths,
record order and addresses. Compatible opcode substitutions are allowed; branch
targets must remain representable and edited scripts are structurally reparsed.
This is an editable assembly/data representation, not recovered high-level source
or a compiler. Assembly `.asm` listings remain inspection-only. Name tables can
change string lengths and rebuild offsets; suffix and alternate-input strings
currently preserve encoded lengths.

JSON strings use a reversible CP437-to-Unicode display mapping. This preserves
engine bytes; a game's custom font or encoding may give those bytes different
visual meanings. Characters outside the mapping are rejected. Unknown instrument
parameters and section bytes retain numeric names rather than invented meanings.
Import does not prove that edited grammar, references, patches or bytecode behave
correctly in an interpreter.

Unknown/malformed layouts use `uninterpreted-byte` records, with consecutive
`address_0000` IDs, offsets, and integer values 0..255. Memory uses `memory-byte`.
These arrays can be resized up to 65,535 bytes, preserving consecutive identities.
They are machine-readable byte editing, **not semantic decoding**. Changed packaged
SCI0 records remain limited to 65,531 bytes. JSON input is limited to 16 MiB.

For automation, use any JSON library or `jq`; preserve metadata and record IDs.
For example, a named record can be selected by its `id` and a documented editable
field updated. Validate against the companion schema, then import to apply the
additional structural checks. Standard PNG, MIDI/WAV and escaped TSV workflows
remain the preferred formats for images, sounds, text and vocabulary 0.

Format references: [ScummVM vocabulary implementation](https://github.com/scummvm/scummvm/blob/master/engines/sci/parser/vocabulary.cpp),
[AdLib driver](https://github.com/scummvm/scummvm/blob/master/engines/sci/sound/drivers/adlib.cpp),
[MIDI patch driver](https://github.com/scummvm/scummvm/blob/master/engines/sci/sound/drivers/midi.cpp),
and [JSON Schema 2020-12](https://json-schema.org/draft/2020-12/json-schema-core).
