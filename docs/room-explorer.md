# Room explorer

```sh
scimate rooms ./game
scimate rooms ./game 42
scimate rooms --json ./game > rooms.json
scimate help rooms
```

The explorer parses sectioned SCI0 scripts, maps vocabulary 997 selector names,
and follows class ancestry to identify Rm/Room instances. Each candidate has a
stable script/object ID, initial picture, directional exit properties, and
initial view-bearing objects from the same script. Constant newRoom/startRoom
send packets provide additional clues; branches and calls invalidate constants.
First duplicate resources win, matching runtime lookup. Malformed scripts,
missing symbols, ambiguous classes and absent target rooms produce diagnostics.

An exported room at slot zero uses its script ID as a **likely** room number,
following the ScriptID convention. Otherwise a nonzero initial number property
is used. `number_basis` states the evidence and `initial_number` preserves the
decoded value, which may be zero until initialization. Room numbers are not
universally equivalent to script IDs. Unknown numbers remain null.

JSON output has format `scimate-rooms`, version 1, `static_analysis: true`,
`shadowed_entries`, `rooms`, and `diagnostics`. Room records include `id`, `name`,
`script`, `offset`, `number`, `initial_number`, `number_basis`, `picture`, `exits`,
`objects`, and `notes`. Object properties retain unsigned engine word values;
coordinates such as x/y/z are interpreted as signed 16-bit values by the preview.

This is static inspection, not interpreter execution. Conditional transitions,
dynamic initialization, room ownership of objects, cast membership, animation,
priority occlusion and actual playability are not established. Same-script
objects and sends may belong to a different scene. Pictures are taken from
initial properties; no same-number picture fallback is invented.

The room/selector conventions were checked against the local SCI Companion SCI0
template's `src/Game.sc` and `src/rm001.sc`, and
[ScummVM's selector mapping](https://github.com/scummvm/scummvm/blob/master/engines/sci/engine/selector.cpp).
The implementation reuses SCIMate's bounded script parser and independently
implements room discovery; it does not embed a ScummVM interpreter.
