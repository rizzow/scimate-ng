# Generic SCI1 VGA migration

The migration target must use a coherent graphics and scripting profile. A VGA
picture installed into an EGA game does not enable VGA mode: picture decoding
uses the resource manager's global view format.

## Graphics

`write_sci1_vga_picture` converts a rendered room's visual, priority and control
planes to a VGA bitmap picture. `write_sci1_vga_view` converts decoded sprite
views to SCI1 VGA, retaining loop/cel numbering, mirrors, placement and transparent
indices. Views use the game's shared palette; pictures provide that palette.
The first stage retains the original sixteen colours. All pictures and views
must be converted before publishing a VGA game candidate.

The VGA view header uses the format's 0x80 flag, compressed cel data and absolute
16-bit loop/cel offsets. The decoder accepts literal, repeat and transparent runs.
Unrepresentable resource sizes fail instead of truncating pointers. Source EGA
bytes are never reused for VGA output.

## Parser and identity

ScummVM allocates its native parser for SCI0/SCI1 EGA and two recognised game
identities. An unknown SCI1 VGA game has no native parser. Keeping Parse/Said
kernel calls while changing the graphics profile would therefore be unsafe.

The remaining export work requires script implementations of tokenisation,
grammar parsing, Said matching, active synonyms, pronoun state and failure
callbacks. Calls must be redirected to those implementations while preserving
argument frames, accumulator results and event claiming. Original vocabulary,
Said data and synonym sections supply the semantics; game names, script numbers
and selector names must not serve as compatibility exceptions.

The portable parser now has an internal bytecode builder and emitted lexicon
procedures. They generate sectioned SCI scripts with wide branch/call fixups,
export tables, dictionary locals and relocated string pointers. Dictionary
lookup runs as SCI bytecode through the standard StrCmp kernel; it returns a
one-based record index, followed by separate class/group accessors. Final SCI0
meanings win when spellings repeat. This is an internal building block, not yet
a replacement for Parse or Said.

Grammar and suffix readers preserve source rule IDs, start symbols, semantic
tags, class masks and suffix transformations. Their data can be emitted directly
as script locals. Malformed or undefined grammar references are rejected rather
than replaced with a guessed grammar. No private game's vocabulary or bytecode
is embedded in these helpers or their synthetic fixtures.

The exported game also needs a distinct game-object name with relocated string
references, so ScummVM does not activate the original title's workarounds. This
does not establish runtime compatibility by itself.

No VGA CLI migration target is exposed yet. Builds, synthetic regression tests
and ScummVM playback remain manual; the codecs' clangd diagnostics do not prove
a playable VGA export.

Format references: [ScummVM view decoding](https://github.com/scummvm/scummvm/blob/master/engines/sci/graphics/view.cpp),
[resource/version detection](https://github.com/scummvm/scummvm/blob/master/engines/sci/resource/resource.cpp),
and [parser kernel behaviour](https://github.com/scummvm/scummvm/blob/master/engines/sci/engine/kparse.cpp).
