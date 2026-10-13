# SCI0 explanation model

`analyze_sci0_game` returns a `GameExplanation`: structured evidence nodes,
resource references, effective resource IDs, diagnostics, and optional focus.
The desktop consumes this model directly. Human-readable labels are presentation
only: resource navigation must not parse `text` or `evidence` strings.
`format_explanation_text` and `format_explanation_json` render the same model.

```sh
scimate explain --json ./game > explanation.json
scimate explain ./game pic 7
jq '.references[] | select(.target.type == "view" and .target.number == 42)' explanation.json
```

## JSON version 1

The root has these fields:

| Field | Meaning |
| --- | --- |
| `schema_version` | Integer `1`; consumers must check this before interpreting fields. |
| `format` | `sci0`. |
| `analysis` | `static`; references do not establish execution or reachability. |
| `focus` | A resource ID or `null`. Focus filters evidence nodes and text references. |
| `shadowed_entries` | Number of duplicate map entries superseded by later entries. |
| `resources` | Effective resource IDs, with first map entry winning. |
| `nodes` | Hierarchical evidence nodes. |
| `references` | Whole-game reference graph, including when focus is supplied. |
| `diagnostics` | Strings explaining skipped malformed resources or ambiguous class definitions. |

A resource ID is an object such as `{"type":"script","number":100}`. Numbers
are decimal integers. Each node contains `text`, `confidence` (`none`, `fact`,
`likely`, or `unresolved`), `resource` (ID or null), `offset` (byte offset within
that resource or null), `loop` (zero-based view loop or null), and `children`.
Nodes without resource metadata may describe their enclosing resource; they
do not introduce new resource references.

Each reference contains:

```json
{
  "source": {"type": "script", "number": 100},
  "target": {"type": "pic", "number": 7},
  "offset": 42,
  "confidence": "likely",
  "evidence": "script 100 @ $002a initial picNumber = 7",
  "target_exists": true
}
```

The offset identifies the source instruction or object. Outgoing lookup matches
`source`; reverse lookup matches `target`. Distinct sites are retained, including
multiple uses from the same script. Absent targets remain in the graph.

## Recognition and limits

The index recognizes initial `view`, `pic`, `picture`, and `picNumber` properties,
constant assignments through those selectors, `callb`/`calle` script calls,
class loads, superclass sends, and object class/inheritance dependencies.
Property and selector meanings are conventions and receive `likely` confidence;
explicit bytecode/class dependencies receive `fact`. Neither proves execution.

Selector vocabulary 997 and unambiguous class layouts are required for property
semantics. Constants are tracked only within straight-line basic blocks; branches,
calls, malformed send packets, and unknown operations invalidate that state.
Room numbers are not assumed to be script IDs. Arbitrary numeric constants do
not become links. Dynamic IDs, general kernel-call arguments, interprocedural
flow, and external patch files are not indexed. An empty reverse lookup means
no recognized static reference, never proof that a resource is unused.

Malformed scripts/views produce partial analysis and diagnostics. Duplicate IDs
use only the first map entry, including when that entry is malformed. JSON output
is deterministic for the same loaded game and focus.
