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

## Generic compatibility constraints

`scimate migrate --to sci1 <source-game-directory> <new-destination>` currently:

- Strictly validates sectioned SCI0 scripts and main export 0's object target,
  without checking or interpreting the object's name.
- Resolves SCI0 duplicate IDs using first-entry semantics documented by the
  ScummVM SCI0 resource loader.
- Applies the documented standard SCI0-to-SCI1 kernel numbering transformation.
- Bridges FOpen/FPuts/FGets/FClose through SCI1 FileIO suboperations, validating
  argument counts and relocating the expanded main export table.
- Refuses parser calls (Parse/Said), SCI0 DoSound calls, DoAvoider, unsupported
  kernel IDs, address widening and unsupported layouts. These require semantic
  adaptations, not game-identity exceptions or selector-name guesses.
- Reopens the generated container to verify resource identities and payloads.

**Consequently, parser/sound games are not yet convertible by this draft.**
Removing a title check does not solve those missing adaptations. They must be
implemented generically before the requested real-world playback proof is met.
Output is explicitly UNVERIFIED. No interpreter compatibility is inferred from
successful serialization or a user-selected `--format` argument.

## Manual validation gate

```sh
cmake -S . -B build-sci1 -DCMAKE_BUILD_TYPE=Debug -DSCIMATE_BUILD_DESKTOP=OFF
cmake --build build-sci1 --config Debug
ctest --test-dir build-sci1 -C Debug --output-on-failure
./build-sci1/scimate inspect /path/to/private/source-game
./build-sci1/scimate migrate --to sci1 /path/to/private/source-game ./build-sci1/migrated-game
./build-sci1/scimate inspect --format sci1 ./build-sci1/migrated-game
```

These are instructions for manual execution, not evidence that they passed.
The destination must not exist. On a reported incompatibility, implement and
validate the missing generic adaptation rather than bypassing the check.

Once conversion succeeds, add the generated directory to unmodified ScummVM.
Record the ScummVM version and configured game path. Its debugger `version`
command must show SCI1 late, with map, volume, LOFS, graphics and sound modes
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
No ScummVM source, interpreters, detection tables or game assets are bundled.
