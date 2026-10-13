# Other editable SCI0 resources

Game export now creates `assets/NNNNNNN/` for each resource other than views and
pictures. The ordinal matches `payloads/NNNNNNN.bin`; duplicate IDs remain separate.
Each bundle includes `asset.tsv` (identity/representation) and `original.bin`.
Keep these two files unchanged. Import reads the editable files described below,
compares their reconstructed payloads to the snapshot, and combines the edits
with the existing resource reconstruction workflow.

| Type | Editable files | Supported layout |
| --- | --- | --- |
| Sound | `sound.mid`, `channels.tsv`, optional `sample.wav` | SCI0 late 33-byte or early 17-byte header |
| Font | `font.tsv`, `glyph-N.png` | Offset table and monochrome glyphs |
| Cursor | `cursor.tsv`, `cursor.png` | SCI0 16×16 two-mask cursor |
| Text | `strings.tsv` | NUL-terminated byte strings |
| Vocabulary 0 | `words.tsv` | SCI0 parser dictionary with shared prefixes |
| Script, known vocabularies, supported patch banks | `resource.json`, `resource.schema.json` | Named structured records |
| Memory, unknown/malformed layouts | `resource.json`, `resource.schema.json` | Explicit byte-level records |

Unsupported media conversions retain byte-level JSON with `conversion.error.txt`.
See [structured resource editing](structured-resources.md) for JSON layouts and limits.
Raw payloads under `payloads/` remain available for every resource. Script `.asm`
listings are still inspection-only; source compilation is not implemented.
The desktop's **Open Resource Folder** opens the selected bundle. Edit externally,
then **Refresh from Disk** and build to a new folder.

Standalone commands:

```sh
scimate sound-export sound.bin new-sound-folder
scimate sound-import new-sound-folder edited-sound.bin
# font-export/import, cursor-export/import and text-export/import work likewise.

scimate resource-export vocab 0 vocab.bin new-vocab-folder
scimate resource-import new-vocab-folder edited-vocab.bin
# resource-export accepts any SCI0 type name and number 0..2047.
```

The type-specific export aliases set the standalone bundle's resource number to
zero; use `resource-export` to record another number. Inputs are decoded payloads,
not standalone patch files or compressed volume records. All destinations must
be new. Existing game exports without an `assets/` directory remain importable.
To add a bundle to an old export, use the record's type/number and its original
payload, and choose `assets/NNNNNNN/` as the destination.

## Sounds

MIDI export uses format 0, 30 ticks per quarter and 500,000 microseconds per
quarter, preserving SCI's 60 ticks per second. Import accepts MIDI format 0 or
1 with PPQN timing, merges tracks, follows tempo changes, and rounds event times
to the nearest SCI tick. SMPTE timing, escaped/split SysEx, malformed tracks,
and unsupported system messages are rejected. Valid complete SysEx is retained.
Tempo changes affect timing; other non-timing MIDI metadata is ignored.

Channel 15 program changes and controllers carry SCI loop/cue messages. Keep
these if scripts depend on them. MIDI contains instrument/program events, not
synthesized audio; playback depends on the selected MIDI device and its patches.
`channels.tsv` retains all sixteen voice/device byte pairs in the original
header. Change `voices` or `devices` (0..255) to route newly used MIDI channels;
new notes on an originally disabled channel may otherwise remain silent.
The sample flag and unknown header bytes are preserved.

When the original sound contains a supported digital sample, `sample.wav` is
unsigned 8-bit mono PCM. Import requires this format and a sample rate of
1..65,535 Hz; it accepts standard RIFF chunks and padding. It does not resample
or quantize. Edits update the sample rate/length while preserving unknown sample
header fields, repeated stop markers, and trailing bytes. Adding a PCM section
to a MIDI-only sound is not supported. Export checks both early SCI0 17-byte and
late 33-byte layouts. Exactly one must decode successfully; ambiguous payloads
retain byte-level JSON with a diagnostic. Existing explicit MIDI bundle layouts
remain importable. Early
voice/device fields are packed nibbles (0..15), rather than byte pairs.
Unsupported SCI1 sound layouts use byte-level JSON.

## Fonts and cursors

Font PNGs are opaque black and white EGA pixels. `font.tsv` specifies line height
(1..255) and each glyph's dimensions, which must match the PNG. Glyphs can be
0..255 pixels wide and 0..200 high. Empty glyphs use a white PNG whose dimensions
are `max(1,width) × max(1,height)`; the metadata preserves their zero dimensions.
Keep glyph indices, filenames, and the number of glyphs unchanged. To resize a
glyph, resize its PNG and update its dimensions. Glyph pixel editing does not
assign Unicode character identities; character indices are engine byte values.

Cursor PNGs are 16×16, black/white with binary transparency. Opaque use of the
transparent EGA index 3 is rejected. `cursor.tsv` allows hotspot `0` for (0,0)
or `8` for (8,8); SCI0 cannot represent arbitrary hotspots or gray pixels.

## Text and dictionary tables

Tables use ASCII with byte-preserving escapes: `\\`, `\n`, `\r`, `\t`, and
`\xNN`. For example, a DOS character byte 0x82 appears as `\x82`, not a guessed
Unicode character. Embedded NUL is rejected in text. Keep text indices sequential
from zero; adding/removing rows changes the indices referenced by scripts.

Dictionary rows contain `word`, `class`, and `group`. Words are sorted 7-bit byte
strings up to 255 bytes; class and group are 0..4095. Duplicate words are retained.
Import rebuilds shared prefixes and the alphabet offset table. Vocabulary
resources other than number 0 have different engine-specific layouts; known
layouts use structured JSON. No semantic validation of grammar/class/group choices
or script references is performed.

## Preservation and bounds

Unchanged bundles reproduce original resource bytes, including padding, unused
font bits, alternate cursor white-mask encodings, MIDI running status, and trailers.
Conflicting edits to a bundle and its `payloads/` file are rejected; equal edits
are accepted. Binary payload edits alone remain supported. Editable serializers
validate their reconstructed content before saving; this does not prove gameplay
or interpreter compatibility.

Inputs and output sizes are bounded. Decoded SCI0 payloads are at most 65,535
bytes; edited packaged stored records remain limited to 65,531 bytes. MIDI/WAV
and PNG input files are at most 16 MiB; tables are at most 1 MiB. MIDI supports at
most 256 tracks and 65,535 emitted events. Engine playback and platform runtime
behavior have not been verified.
