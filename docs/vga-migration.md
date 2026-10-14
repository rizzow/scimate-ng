# Generic SCI1 VGA migration

`scimate migrate --to sci1 --graphics vga <source-game> <new-destination>`
now selects an experimental VGA export. The default remains the existing EGA
compatibility export. Neither path establishes playback proof.

The VGA path writes the early SCI1 packed-ID map and eight-byte volume headers,
converts every view and picture, installs generated parser scripts and assigns
the main game object the name `scimate-vga`. The name change is independent of
the source title and uses an appended string with the original relocation slot.
Private game code and assets must remain outside version control.

## Manual use

Build and test manually, then run:

```sh
./build/scimate migrate --to sci1 --graphics vga /path/to/source-game /path/to/new-vga-game
./build/scimate inspect --format sci1-vga /path/to/new-vga-game
```

`sci1-vga` and `sci1-ega` read the same packed container format. Their payloads
determine the graphics/interpreter profile. Add the output directory as a new
game in unmodified ScummVM and capture its detection/debugger version output.
The required proof includes an unknown game identity, SCI1 VGA graphics,
startup, movement, typed commands, synonyms, room transitions, sound and
save/restore. Serialization and clangd diagnostics are not playback evidence.
The real-game VGA export and this runtime proof remain outstanding.

## Script and parser conversion

The existing kernel remap, FileIO bridges, sound checks, volume-query repairs
and verified missing-script fallbacks run first. Parse/Said/SetSynonyms calls
then become equal-width main-export calls. Appended main forwarders preserve
the actual parameter lists with `rest 1`; helper script IDs use wide operands.
Replacement export tables are appended rather than moving source addresses.

The portable parser compiles the source vocabulary, grammar, suffixes and Said
specifications into ordinary SCI bytecode. It performs word scanning, suffix
lookup, ordered sentence derivation, tagged-tree construction, Said matching,
pronoun replacement, event claiming and source failure callbacks. Its standard
VM/string/list/memory kernels do not require ScummVM's native parser.
Said lookup trees are deduplicated and partitioned by exact resource size.

Synonym selection copies ordered source pairs. A live object's injected owner
method identifies its loaded owner script; clones retain that method. A target
with aliases must be main or have a live owner witness in the supplied list or
the source game's global slots. The global range comes from script 0's locals;
no room/global IDs are hardcoded, and IsObject filters non-object values.
Otherwise selection clears partial aliases, stores a failure and prevents a
successful parse. It never uses ScriptID to force-load a synonym source.
Scripts without aliases need no eligibility proof. This bounded witness
mechanism does not cover every script loaded elsewhere in the VM.

Source map duplicates use the first entry, synonym blocks use the first block,
and exports use the last table. A populated object header determines the active
method table. Owner methods replace dispatch tables without adding properties
or moving existing code. Selector collisions and old references into appended
regions are rejected. Zero method-area headers remain an inspection-only legacy
inline convention, not evidence of native interpreter compatibility.

Helper IDs reserve source resource IDs, external calls, immediate literals,
object values and class-to-script mappings. Computed script/selector IDs remain
unverified. No title-specific script IDs, selector IDs, globals or detection
hashes establish compatibility.

## Graphics conversion and runtime guards

Views retain loop/cel IDs, mirrors and placement in the SCI1 VGA encoding.
Without PNG overrides, their transparent indices also remain unchanged.
For `import --target-engine sci1 --graphics vga`, the main view PNGs accept VGA
RGB colours directly. Alternatively, a view bundle may contain a complete
`vga/` directory with matching `loop-L-cel-C.png` names and a byte-identical
copy of its parent `view.tsv`. Mirrored loops keep their source references and
have no separate PNGs. A `vga/view.tsv` selects the sibling set; when selected,
the parent EGA PNGs, metadata and binary payload must remain unchanged.
Without a sibling manifest, the parent PNGs are decoded as VGA. Conflicting
binary payload and PNG edits fail. Unchanged EGA PNGs retain their original
payload, so binary-only edits remain supported.

Indexed, RGB and RGBA PNGs with binary alpha are accepted. One embedded palette
is allocated across every source cel, retaining EGA colours 0–15, adding up to
239 RGB colours and mapping transparent pixels to index 255. No automatic
quantization or dithering occurs. Reduce all frames together before import;
PNG dimensions control cel dimensions, so preserve them for registered art.
Missing cels, partial alpha, excess colours and changed mirror metadata fail.
Duplicate resource IDs retain the existing first-record selection policy;
place the override in the selected bundle. SCI0/EGA import ignores `vga/`.
Embedded view palettes are merged and remapped by the SCI1 VGA renderer; colour
fidelity still depends on runtime palette pressure and needs playback review.
Existing CLI binaries need rebuilding to use this source change.

Pictures provide a shared palette retaining the original
sixteen colours. Flood-filled backgrounds use an embedded bitmap plus vector
priority/control planes. Pictures without fills use explicit vector drawing
masks, preserving untouched pixels and white/zero writes.

DrawPic calls pass through a generated bridge. Where a supplied overlay flag
needs translation, the source SCI0-late ABI is determined from the interpreter's
Rm.overlay detection point; an unresolved implementation fails export rather
than using a title-based fallback. The bridge preserves incoming accumulator
and actual arguments. Flood-fill overlays, nonzero EGA palette selections and
mirrored pictures display an explicit unsupported-operation message and are not
drawn. These cases still need full semantic conversion.

Picture 200 is converted by the same generic picture path. Its earlier standalone
private patch remains under ignored `migration-output/pic200-vga/`; it is not
installed into the EGA export. The VGA migration converts the source picture
directly and does not depend on that private patch.

## Remaining limits

- Builds, regression execution and real-game playback remain manual.
- Foreign loaded-script synonym targets without a live list/global witness fail parsing.
- Dynamic/uncatalogued Said specifications claim the event, invalidate the parser
  and display an explicit error; they are not treated as ordinary failed matches.
- Translated/secondary vocabularies need an explicit language profile.
- Grammar force-storage and multiple-immediate-terminal productions are unsupported.
- Parser limits are 255 bytes per word, 64 words, 4096 input bytes, 1024 symbols
  per candidate, 512 distinct candidates and 500 parsed tree nodes.
- Resource sizes and signed branch spans are checked; oversized output fails.
- Picture overlay/palette/mirroring guards are incomplete gameplay support.
- Legacy invalid branches and dynamic export accesses remain runtime-unverified.

The formal goal remains incomplete until the real private game is exported and
manual ScummVM evidence proves the requested VGA identity and gameplay.

## Format references

Primary format/behaviour references are ScummVM's
[resource/version detection](https://github.com/scummvm/scummvm/blob/master/engines/sci/resource/resource.cpp),
[script loading](https://github.com/scummvm/scummvm/blob/master/engines/sci/engine/script.cpp),
[object dispatch](https://github.com/scummvm/scummvm/blob/master/engines/sci/engine/object.cpp),
[parser kernels](https://github.com/scummvm/scummvm/blob/master/engines/sci/engine/kparse.cpp),
[graphics feature detection](https://github.com/scummvm/scummvm/blob/master/engines/sci/engine/features.cpp),
[picture drawing](https://github.com/scummvm/scummvm/blob/master/engines/sci/graphics/picture.cpp)
and [view decoding](https://github.com/scummvm/scummvm/blob/master/engines/sci/graphics/view.cpp).
The generated runtime is independently implemented; no ScummVM parser source
or private game data is embedded in the repository.
