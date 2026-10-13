# Generic SCI0 to SCI1 migration

The mechanism must depend on documented formats and verified compatibility,
never a game title, object name, hardcoded resource list, or game-specific
ScummVM exception. A real game may be used privately to validate the mechanism;
it does not define migration rules.

**Status: incomplete experimental implementation; no playback proof.** Builds,
tests and playback remain manual. VGA work is deferred until a real SCI0 game is
exported and played in an unmodified ScummVM reporting an SCI1 interpreter.

## Format support

Explicit `sci1` selects DOS SCI1 late directory maps with six-byte entries and
nine-byte volume headers whose packed lengths include four bytes. `sci11`
selects SCI1.1 maps with five-byte entries and word offsets, and nine-byte headers
whose packed lengths exclude those four bytes. Compression methods 0 (stored),
1 (Huffman) and 2 (MSB LZW) are supported; other methods fail explicitly.
Writers produce uncompressed records. Resource types through heap are supported.

Container support does not imply full interpreter or payload conversion.
SCI1.1 script/heap migration, other map variants, VGA resource conversion and
DCL/rearranged LZW are not implemented.

Parser-bearing migrations use the SCI1 EGA profile: packed resource IDs and
six-byte map records, eight-byte volume headers, and stored payloads. Inspect
these using `--format sci1-ega`. SCI1 EGA is a SCI1 interpreter generation; it
does not use SCI1.1 split script/heap resources or VGA artwork.

## Generic compatibility constraints

`scimate migrate --to sci1 <source-game-directory> <new-destination>` currently:

- Validates sectioned SCI0 script layouts, method and local-call targets, main-script
  branches, and main export 0's object target,
  without checking or interpreting the object's name.
- Preserves opaque, uncalled export slots. SCI export tables can contain data
  addresses or unused legacy values; they do not all identify decoded procedures
  or objects. Direct CALLB/CALLE targets must identify decoded instructions.
  Main export expansion relocates in-range data addresses and leaves out-of-range
  opaque values unchanged. Dynamic export accesses remain unverified and are
  listed as a manual-validation limitation in the migration report. Errors
  identify the script number and, for direct calls, the instruction offset.
- Specializes an optional missing-script call only when a verified alternate
  arm already exists: a direct global load and conditional branch guard an
  argument-preparation/CALLE/jump arm, followed by a contiguous fallback arm
  with a call or send and a shared merge point. No method/export or branch may
  enter the disabled arm separately. The global load becomes an equal-width
  `ldi 0`, selecting the original fallback with the original false-path
  accumulator value. Global storage is not modified. The report identifies
  the script, call, guard and fallback offsets. Missing unguarded dependencies,
  malformed existing exports, side effects in the optional arm, and alternate
  entries remain errors. This rule depends on resource availability and code
  structure, never fixed script/global IDs or a game title.
- Preserves legacy non-main branch operands even when their targets fall outside
  decoded instructions. These may be compiler artifacts, but their runtime
  reachability is not assumed. Kernel rewrites must preserve all instruction
  boundaries/extents and branch operands; migration verifies that invariant.
  Every invalid target is listed in the report as UNVERIFIED. Main-script branch
  targets remain strict because export expansion moves its addresses. Normal
  inspection and the SCI1.1 split transform retain strict branch validation.
- Resolves SCI0 duplicate IDs using first-entry semantics documented by the
  ScummVM SCI0 resource loader.
- Applies the documented standard SCI0-to-SCI1 kernel numbering transformation.
- Bridges FOpen/FPuts/FGets/FClose through SCI1 FileIO suboperations, validating
  frame alignment and relocating the expanded main export table. Bridges add
  the FileIO suboperation and forward the actual parameter list with `rest 1`.
  No omitted parameters are read and no default open mode is guessed. Unusual
  encoded counts are preserved and reported for runtime signature validation;
  malformed source calls do not become silently repaired or valid calls.
- Supports a ScummVM SCI0 late sound compatibility profile: validates sound
  resources with the SCI0 late decoder and statically proves DoSound suboperation
  and argument-count frames before remapping the kernel to 0x2d. Resource bytes
  and sound suboperation numbers are retained. Vocabulary 997 must be present
  and omit nodePtr, which is ScummVM's documented SCI0 late dispatch condition.
  This condition verifies target dispatch; it does not infer source sound format.
  Dynamic/unproven call frames, early sound and incompatible dispatch fail.
  Argument computation may include accumulator-only `bnot`, `neg` and `not`:
  these preserve previously pushed count/suboperation constants. Stack-consuming
  arithmetic remains unsupported by this conservative backward scan. Errors
  identify the containing script and instruction offset.
- Adapts parser-bearing games to SCI1 EGA, the version where unmodified ScummVM
  enables the parser without title-specific exceptions. Parse/Said/SetSynonyms
  calls retain their ABI after argument-frame checks. Word vocabulary 0 becomes
  null-terminated SCI1 vocabulary 900, grammar 900 moves to 901, and optional
  suffix vocabulary 901 moves to 902. Word classes/group IDs and grammar/suffix
  bytes are preserved; repeated spellings retain SCI0's final definition.
  Said and synonym sections remain in sectioned scripts and their pointers
  undergo the existing main-script relocation. The obsolete SCI0 kernel-name
  debug vocabulary 999 is omitted because it describes the replaced kernel ABI.
- Refuses DoAvoider, unsupported
  kernel IDs, address widening and unsupported layouts. These require semantic
  adaptations, not game-identity exceptions or selector-name guesses.
- Reopens the generated container to verify resource identities and payloads.

Parser conversion requires a valid EGA view in ScummVM's detection range,
SCI0 word vocabulary 0 and complete 20-byte grammar rules in vocabulary 900.
Conflicting vocabulary 902 and secondary-language vocabulary sets are rejected.
No game-identity exception is used to enable the parser. This remains an
experimental conversion: real-world playback proof has not been established.
Output is explicitly UNVERIFIED. No interpreter compatibility is inferred from
successful serialization or a user-selected `--format` argument.

## Manual validation gate

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
./build/scimate inspect /path/to/private/source-game
./build/scimate migrate --to sci1 /path/to/private/source-game ./build/migrated-game
./build/scimate inspect --format sci1-ega ./build/migrated-game
```

These are instructions for manual execution, not evidence that they passed.
The destination must not exist unless `--force` replaces its entire directory.
Output is generated before replacement; symbolic-link destinations and the
source directory or its ancestors are refused. On a reported incompatibility, implement and
validate the missing generic adaptation rather than bypassing the check.

Once conversion succeeds, add the generated directory to unmodified ScummVM.
Record the ScummVM version and configured game path. Its debugger `version`
command must show SCI1 EGA for parser-bearing output (SCI1 late otherwise), with
map, volume, LOFS, graphics and sound modes
captured too. Demonstrate startup, walking/animation, interactions, room
transitions, sound, and save/restore. Parser gameplay must work through a generic
adaptation; title-specific ScummVM exceptions do not establish generic support.
Record screenshots/logs and differences. A title screen alone is insufficient.

## Game-data policy

Commercial game code, bytecode, decompiled scripts, extracted assets and migrated
outputs must never be committed. Use private external directories or ignored
`private-games/`, `LSL3/` and build directories. Tests contain independently
constructed synthetic byte sequences with invented names. Real-game validation
results may describe behavior, but must not embed the game's source or assets.

## References

Format/compatibility facts were inspected in ScummVM's
[resource loader](https://github.com/scummvm/scummvm/blob/master/engines/sci/resource/resource.cpp),
[kernel numbering](https://github.com/scummvm/scummvm/blob/master/engines/sci/engine/kernel.cpp),
[kernel signatures](https://github.com/scummvm/scummvm/blob/master/engines/sci/engine/kernel_tables.h),
and [debugger](https://github.com/scummvm/scummvm/blob/master/engines/sci/console.cpp).
Sound compatibility follows the independent sound-version selection in
[features.cpp](https://github.com/scummvm/scummvm/blob/master/engines/sci/engine/features.cpp)
and SCI0 sound signatures in the kernel table. It targets ScummVM and does not
claim compatibility with the original Sierra SCI1 sound driver.
No ScummVM source, interpreters, detection tables or game assets are bundled.
