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

The grammar compiler expands leading nonterminals into terminal-led productions
before export. Semantic tags and pending nonterminals stay in each production;
the generated runtime can consume words and extend trees using word-sized
kind/value symbols. Alternative ordering follows source production order and
expansion depth. Normalized tables use local-index offsets, not native pointers.
Expansion limits reject oversized or infinitely recursive derivations instead
of truncating them. The legacy debugging force-storage opcode remains explicitly
unsupported pending a semantic proof.

An emitted SCI procedure selects matching productions from the normalized table
using the source class/group masks. Its caller can enumerate alternatives from a
given index, preserving ambiguity rather than taking the first matching word
alone. This procedure uses ordinary VM arithmetic and local-variable operations;
it does not call the native Parse/Said kernels. Said evaluation and game callback
integration still need to be connected before VGA export is enabled.

The emitted input scanner and word resolver now use the standard string kernels
only. The scanner retains SCI0's default OEM case folding and input rules: literal
spaces end words, other punctuation is ignored, and hyphens can occur after a word
starts. Exact lookup strips hyphens; suffix reduction uses the original lowercased
word. The resolver applies the source suffix order and output class, then recognises
complete decimal words with the numeric word group. Buffers remain in generated
script locals, so failure callbacks can safely read them after the routine returns.
The current scanner limits are 255 bytes per word, 64 words and 4096 input bytes;
failures clear the token count and record a reason instead of truncating input.
Alternative language/code-page handling still needs an explicit export profile.

The sentence controller now emits SCI code that keeps ordered alternative parses
in standard interpreter lists. Candidate streams retain tree tags, consumed word
groups and pending nonterminals. Rule expansion allocates an independent stream;
failed alternatives are freed and the first complete derivation becomes the
result. The previous result is released at the next parse. Candidate pointers
remain in VM registers/list nodes rather than being truncated into raw words.
The current bounds are 1024 symbols per stream and 512 active candidates; capacity
failure is distinct from syntax failure. Grammar productions with multiple
immediate terminals fail export until their execution semantics are supported.
These emitted routines still require manual runtime validation and export wiring.

Worklists compare complete candidate streams before appending. Identical
derivations are freed, the first alternative keeps its position, and only distinct
streams count toward the capacity limit. Equality includes semantic tags and word
groups, so candidates with different Said meanings are retained.

The Said front end decodes bounded specifications into tagged trees. It retains
sentence-part boundaries, comma alternatives, references, nested parentheses,
optional terms and the partial-match marker. Word groups use the resource's
big-endian byte order. Invalid specifications fail explicitly; conjunction/hash
operators are tokenised but are not accepted by the native-compatible grammar.
Said trees now serialize into immutable six-word node records in script locals.
The emitted matcher reads parsed nodes from a separate dynamic arena and keeps
three internal outcomes: absent, mismatched and matched. Optional terms accept
absence, not a present mismatching word. Alternatives, reference tags, wildcard
groups and the partial marker remain distinct. A partial match sets state for
the future call bridge to preserve event claiming. Specification lookup and call
redirection still need integration.

The parsed-tree builder now converts complete candidate streams to six-word
records in a bounded dynamic arena, with separate child and sibling indices.
It validates paired tags and balanced delimiters and rejects mixed terminal/
child payloads. Root tags come from the source grammar. Failure frees the arena.
The unified parser entry emits scanning, sentence derivation and tree conversion
in one pipeline, releasing prior results before new input. It returns an arena
pointer or zero and exposes failure state for the eventual event/callback bridge.
The candidate stream is released immediately after tree conversion.
This entry is still internal; it has not enabled a playable VGA CLI export.

Pronoun state now persists in generated script locals. Before grammar parsing,
pronoun-class tokens become nouns referring to the previous successful sentence's
subject word group. After a successful tree build, the subject lookup follows the
format's immediate-child/first-generic-child traversal. Sentences without a new
subject and failed input retain the old reference. The initial undefined marker
disables replacement until a reference has been established. Synonym collection
and the event/callback bridge remain to be connected.

Generated Parse/Said bridges now retain the current event and parser-valid state.
Parse claims failed input and invokes the source game's wordFail/syntaxFail
selectors; successful input remains unclaimed. Empty input has no syntax callback.
Said skips claimed/invalid events, claims full matches and retains partial matches
for subsequent handlers. Selector IDs are supplied by source-vocabulary lookup,
not fixed numeric identities. Failure text uses a persistent local buffer. The
bridges still need Said lookup pages, synonym collection and export call rewriting.

Said lookup pages now emit deduplicated specification bytes and tagged trees.
A sorted two-byte prefix index narrows candidate specifications before exact
byte comparison through StrAt; an original Said pointer may belong to any source
script. Known specifications produce full/partial/no-match results through the
emitted matcher. An explicit unknown result lets the dispatcher try another page.
The export layer still needs page partitioning and call wiring; dynamic Said
specifications require additional handling rather than an assumed match.

Lookup-page generation now partitions deduplicated specifications by exact
serialized size, including runtime code and tree records. Each page exposes
export zero and obeys the early SCI1 stored-resource size limit. A single
oversized specification fails explicitly. The generated dispatcher calls page
scripts with wide IDs and continues only on the unknown-specification result.
Assigning collision-free script IDs and rewriting the game calls remain export
integration work; page partitioning does not establish runtime proof.

Active synonym state now owns ordered memory blocks of source word pairs. Clearing
frees them; appending copies them independently of source-script lifetime. Token
transformation visits every pair in list order, allowing a replacement to feed a
later alias, and runs before pronoun replacement. Collection remains separate:
the bridge must preserve native loaded-script eligibility. ScriptID loads scripts
and therefore cannot serve as an availability probe. A title-specific list or an
assumption that every mapped script is loaded would change parser semantics.

Export-side parser input collection now reads the first resource-map entry for
each ID and gathers source vocabulary, grammar, suffixes, Said blocks and synonym
pairs. Callback selectors come from vocabulary 997. Helper-script allocation
reserves source resource IDs, direct external calls, literal values, object
properties and class-to-script mappings. Computed IDs still need validation in
the export integration; these reservations are not a proof for arbitrary dynamic
script-number expressions. Secondary/translated vocabularies fail explicitly
until a language profile is implemented.

The runtime writer assembles the parser entry script and Said lookup pages using
those reserved IDs. Its entry exports are Parse, Said, clear-synonyms and
append-synonyms, in that order. It checks the early SCI1 container payload limit
before returning scripts. This assembly is not yet installed by migration:
the source rewrite must supply loaded-script synonym eligibility and redirect
the original parser kernel calls to these exports.

Main-script bridge insertion now appends code and a replacement export table,
retaining all original addresses. Sectioned SCI0/SCI1 uses the last export table;
source inspection follows that rule as well. Synonym collection retains only the
first synonym block, matching the interpreter's block lookup. These format rules
come from [ScummVM's script loader](https://github.com/scummvm/scummvm/blob/master/engines/sci/engine/script.cpp).

The migration bridge writer can forward the full actual parameter list to a
runtime script export using `rest 1`, without narrowing helper script IDs. A
separate main-object identity helper appends a new name string and changes only
the game's name property. It requires that property in the active relocation
table, preserving its segmented pointer at load time. Original strings remain
available to other source references. Both helpers still need to be wired into
the final VGA migration profile; the current CLI export retains its source name.

Object inspection now follows a populated header's relative method-area offset,
and structured method bindings use that active table. The owner-method writer
appends a constant script-number query with replacement dispatch tables stored
as binary strings. It preserves property layouts and original addresses and
rejects selector collisions or old references that would acquire targets in the
appended region. Clones inherit the source dispatch table. This query provides
evidence about a live object's owner; it does not prove that an unrelated script
number is loaded. SetSynonyms selection is still not connected.

The method-area addressing and clone-table behaviour were checked against
[ScummVM object loading](https://github.com/scummvm/scummvm/blob/master/engines/sci/engine/object.cpp)
and its [object representation](https://github.com/scummvm/scummvm/blob/master/engines/sci/engine/object.h).
Zero method-area words remain an inspection-only legacy inline convention;
their acceptance is not proof of native interpreter compatibility.

The exported game also needs a distinct game-object name with relocated string
references, so ScummVM does not activate the original title's workarounds. This
does not establish runtime compatibility by itself.

No VGA CLI migration target is exposed yet. Builds, synthetic regression tests
and ScummVM playback remain manual; the codecs' clangd diagnostics do not prove
a playable VGA export.

Format references: [ScummVM view decoding](https://github.com/scummvm/scummvm/blob/master/engines/sci/graphics/view.cpp),
[resource/version detection](https://github.com/scummvm/scummvm/blob/master/engines/sci/resource/resource.cpp),
and [parser kernel behaviour](https://github.com/scummvm/scummvm/blob/master/engines/sci/engine/kparse.cpp).
