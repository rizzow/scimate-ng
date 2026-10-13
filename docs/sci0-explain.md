# Explain SCI0 games

```sh
scimate explain /path/to/game
scimate explain /path/to/game view 42
scimate explain /path/to/game script 100
scimate explain --help
```

`--format sci0` is optional. Resource numbers are decimal, from 0 through 2047.
Use `--` before paths beginning with `--`. The script focus identifies a script
resource, not a room number. A game overview lists room candidates within scripts.

The command opens the map and volumes, decompresses resources, and performs
static analysis. It neither executes the game nor modifies its resources.
Duplicate resource IDs use the first map entry, matching migration and SCI0 runtime lookup.

## Evidence and uncertainty

Every conclusion carries a label:

- **fact**: decoded metadata or a recognized constant bytecode send. A call's
  presence is a fact; whether the game executes it is not.
- **likely**: a framework convention, object name, class ancestry, or sprite
  shape suggests a role. These are candidates, not recovered author intent.
- **unresolved**: missing symbols, dynamic behavior, or insufficient evidence.

Evidence includes script numbers and hexadecimal object/instruction offsets.
Object and selector names are escaped before printing. No model or network
service is needed to generate a report.

## What is recognized

- Class inheritance, named objects, initial `view`, `loop`, `cel`, picture and
  cardinal-exit properties, using selector vocabulary 997 and class metadata.
- Room instances descending from `Room` or `Rm`. The containing script number
  does not prove the runtime room number. Export 0 pointing to the room object
  provides an additional room-script clue.
- Player objects named exactly `ego`/`gEgo`, descendants of `Ego`, and direct
  assignments of an object to script 0's conventional player global, slot 0.
- Constant `view`, `newRoom`, and `startRoom` sends, and `setCycle` calls. Receiver
  identity is reported where known; global 0 remains a labeled convention.
- Opening-room candidates in `init`/`play` methods of a Game descendant exported
  at slot 0 in script 0. Conditional transitions and introduction scenes may
  produce several candidates. The first screen/playable room remains unresolved.
- View loop/cel counts, mirrored loops, and multi-cel animation possibilities.
  Four multi-cel loops suggest directional walking weakly; frames can be poses,
  and single-cel loops do not prove that a view is static.

The send scanner retains constants only in straight-line basic blocks. It
validates complete selector/argument packets and clears state at branch targets,
branches, calls, method entries, code-section gaps, and unsupported instructions.
Tracking is bounded to 128 stack entries. This intentionally misses complex
expressions rather than fabricating resource associations.

Missing/malformed vocabularies and individual scripts/views produce a partial
report with diagnostics. Broken maps, volumes, decompression, absent focused
resources, and output errors still fail the command. Exit codes: 0 report
(including partial reports), 1 input/output error, 2 usage error.

## Scope and existing tools

This is a small native C++ resource explanation layer, independent of successful
decompilation. It does not resolve interprocedural flow, computed IDs, exact
animation timing, or gameplay reachability. Only map/volume resources are read;
external patches are outside the current game loader's scope.

[sluicebox/sci-tools](https://github.com/sluicebox/sci-tools) offers substantially
broader SCI decompilation, script parsing, and semantic annotation. In particular,
its [RoomAnnotator](https://github.com/sluicebox/sci-tools/blob/main/SCI/Annotators/RoomAnnotator.cs)
connects room names to script constants, and its
[LSL3 annotator](https://github.com/sluicebox/sci-tools/blob/main/SCI/Annotators/Lsl3Annotator.cs)
provides game-specific global, inventory, and flag names. It is a C# library;
using it as a backend would be a separate integration, not an existing capability
of this command. Future richer semantic recovery should evaluate that backend
and its annotations before extending bespoke bytecode analysis here.
