# Runtime compatibility fixes

The first private ScummVM run detected SCI1 EGA and reached gameplay. The log
identified omissions and compatibility concerns addressed by these generic rules:

- Script filename candidates are matched against regular source files without
  case sensitivity. Referenced data files are copied; mapped volumes, output
  metadata, executables and drivers are excluded. Missing candidates are reported.
  This supports filenames in the game directory; dynamic names and subdirectories
  need additional handling. There is no game-specific filename list.
- FClose bridges save and restore the incoming accumulator to retain SCI0's
  return semantics instead of SCI1's success value.
- Control-flow analysis tracks allocation and direct/indexed assignment of
  temporary variables. A master-volume setter with a definitely uninitialized
  direct temporary argument becomes a query; ambiguous or initialized setters
  are retained. No fixed script, variable or game identity is used. Repairs
  preserve byte addresses and are recorded in the report.
- Source class/instance property-count mismatches are reported and preserved.
  No properties are discarded solely to suppress engine diagnostics.

Synthetic regression fixtures are included. Builds, tests and playback remain
manual; clangd diagnostics are not execution evidence.
