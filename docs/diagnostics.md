# Error reporting and output recovery

Core/application code throws errors with format, resource, or filesystem context.
It does not display dialogs or log every throw: recoverable conversion failures
remain in export diagnostics, and the CLI or desktop reports terminal failures
once through `scimate::log_diagnostic`.

The shared logger writes to stderr by default. It serializes output, escapes
control characters to keep each record on one line, and supports a custom
thread-safe sink through `set_log_sink`. A failing sink falls back to stderr;
logging itself never throws. `exception_message` retains nested exception causes
and reports non-standard exceptions. CLI result tables and JSON remain on stdout.
Desktop errors use the same logger and plain-text dialogs. Worker failures and
exceptions reaching the Qt event boundary are reported through this path.

`export --force` and `migrate --force` generate a replacement in a reserved sibling
directory. The previous destination is renamed into that directory before the
replacement is installed. Failed installation restores the previous destination;
failed rollback preserves its backup and reports recovery paths. Failure to clean
up an obsolete backup is a warning, rather than a failed successful export.
These operations provide recovery from reported I/O errors, not a crash-safe
filesystem transaction or protection against concurrent external directory edits.

Individual resource imports reserve output files exclusively, so an existing or
concurrently created file is never opened for truncation. Related text workspace
updates use `replace_files`: all replacement files are prepared before publication,
and prior publications are rolled back on an error. Project edit validation is
optimistic; callers publish `contents` and `related_writes` immediately after
preparation. It does not provide a cross-process project lock.

Sound export selects an early or late MIDI representation only when exactly one
supported layout decodes successfully. Ambiguous sounds retain byte-level JSON
and a diagnostic. Existing explicit MIDI bundle representations remain supported.
