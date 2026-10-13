# SCI0 map scope and boundaries

The `scimate_core` library exposes standard C++ data and a parser accepting a
byte vector. The CLI owns file I/O and presentation. Dependencies point inward;
Map-only listing needs no application layer. Volume and filesystem workflows
are described in [SCI0 round-trips](sci0-roundtrip.md); the desktop layer remains
future work.

Each SCI0 record is six bytes, decoded explicitly as little-endian:

| Field | Bits |
| --- | --- |
| Resource number | Low 11 bits of the first 16-bit word (0..2047) |
| Resource type | High 5 bits of that word (accepted: 0..9) |
| Volume byte offset | Low 26 bits of the next 32-bit word |
| Volume number | High 6 bits of that word (0..63) |

Types are view, pic, script, text, sound, memory, vocab, font, cursor, and patch.
All encoded number/volume/offset values are retained; conventional resource
number limits and volume sizes are not inferred from a map alone.

Six `FF` bytes terminate a map. A terminator alone is a valid empty map; a
zero-length file is invalid. This reader deliberately requires a complete final
terminator and rejects trailing bytes, partial records, missing terminators,
and types outside SCI0's 0..9 range. These are stricter checks than the legacy
stream iterator. Errors provide a category and zero-based byte offset: the
start of a bad/partial record, EOF for a missing terminator, or the first byte
after a terminator. No partially parsed list is returned.

Duplicates are preserved because the legacy SCI0 writer appends entries and
the map can contain multiple versions of a resource. Listing does not choose
the active version. An all-ones location with a valid ID is a real record,
not a terminator.

Explicit selection is required: SCI Companion separately dispatches the
SCI0/SCI1-layout variant (28-bit offsets and 4-bit volumes). It is unsupported
here. Some other layouts may accidentally pass structural checks, so selecting
`sci0` is a caller assertion, not format identification.
