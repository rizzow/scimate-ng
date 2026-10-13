# Desktop projects

`scimate-desktop` is an optional Qt Widgets application, separate from the
`scimate` command-line tool. Both use `scimate_application` for file operations
and `scimate_core` for SCI data. Qt is only linked by desktop targets.

## Lifecycle

Create a project by selecting an SCI0 game directory, a name, and a destination
folder that does not yet exist. Creation exports the game's resources and then
writes `project.scimate`. The source game is unchanged. Interrupted exports can
leave a partial destination; choose a fresh folder when retrying.

Open `project.scimate` to resume. Opening validates the descriptor, manifest,
original snapshots, and current payload/PNG edits. A failed open or refresh
leaves the previous project loaded. The project can be moved or copied as a
directory: it has no dependency on the source game's absolute path.

Project creation, opening, refresh, and build run on background workers. Project
actions and closing the window are disabled during an operation. There is no
cancel operation in this milestone. Recent paths and window geometry are stored
in the platform's Qt settings location, separately from project contents.

## Editing and building

The resource table retains separate rows for duplicate resource IDs. Its index
column maps to the ordinal names in the export. Filtering covers all columns;
numeric columns sort numerically. `Modified` means the currently loaded payload
differs from the original snapshot, including imported PNG edits.

View previews decode the loaded resource and include mirrored loops. The cel
selector shows loop/cel numbers, and the preview tooltip includes dimensions
and placement. Picture previews select the visual, priority, or control plane. Script previews disassemble the loaded bytecode; other resource
types show at most 512 bytes in hexadecimal, except text resources, which show
a numbered string table. Unsupported previews report a
diagnostic without preventing other resources from being inspected.

Open Resource Folder opens the selected resource’s editable bundle, or its payload
folder for older projects. Save changes in an external editor, then Refresh from Disk.
PNG palette/transparency constraints and binary-versus-PNG conflict detection
are identical to the [CLI workflow](sci0-views.md). A refresh validates the whole
workspace, so any malformed edit must be corrected before it can be reloaded.

Text resources support **Edit text…** and TSV export. The editor uses the CLI’s
`index`/`text` table: one zero-based index and escaped string per row, separated
by a tab. Keep indices/order stable because scripts refer to them. Empty strings
are retained. Newlines, carriage returns, tabs, backslashes, and non-ASCII bytes
use `\n`, `\r`, `\t`, `\\`, and `\xHH`; no character encoding is guessed.
Embedded NULs, malformed escapes, and nonconsecutive indices are rejected.

Save atomically replaces the authoritative text table (or binary payload for
older projects / existing binary edits), updates the preview and Modified status,
and makes the change available to CLI and GUI builds. Cancel discards dialog
changes. Saving refuses stale on-disk content or edits in both binary and asset
files; refresh or restore one representation before retrying. This is an optimistic
conflict check, not a lock against simultaneous external writes. Malformed text
reports a diagnostic and remains available for binary export.

Build Resources to New Folder reads the latest on-disk changes even without a
refresh, verifies reconstruction, writes a new map and referenced volumes, and
reopens them for verification. It refuses an existing destination. Output does
not include interpreters, patch files, configuration, or unreferenced files.
There is no script compilation or game launch action. Runtime behavior of edited
resources still requires testing in an SCI interpreter.

## Format

A project is the existing [SCI0 export format](sci0-roundtrip.md), plus:

```text
SCIMATE-PROJECT 1
"My project"
```

The descriptor is named `project.scimate`. The name uses C++ `std::quoted`
escaping for quotes and backslashes, is limited to 200 UTF-8 bytes, and cannot
contain control characters. Unknown descriptor versions are rejected. The
descriptor is written last after successful export. Original snapshots and the
manifest must remain unchanged.

The command-line workflow remains available:

```sh
scimate import /path/to/project /path/to/new-resource-output
```

Decompiler output from separately installed sci-tools may be kept elsewhere;
SCIMate neither invokes that tool nor imports its `.sc` source files.
