# SCI1.1 script/heap transform

`scimate::split_sci0_script` in `scimate/sci11_script.hpp` structurally converts
a decoded SCI0 script into a SCI1.1 script hunk and heap. It returns both byte
arrays and mappings from source code and heap addresses to their output addresses.
`validate_sci11_script_pair` checks the resulting layout and pointer targets.

The transform creates object property and method dictionaries, separates locals,
objects and strings into the heap, relocates exports and methods, rewrites relative
code references, converts LOFS to heap pointers, and adjusts property offsets for
SCI1.1's five additional object words. Relocation tables are emitted for both files.
All layout and pointer decisions derive from the input format; there are no game
identity checks. Unsupported sections or pointer kinds cause an error.

This API performs structural conversion only. It is not wired into the whole-game
`migrate` command, which currently targets SCI1 late. Kernel ABI adaptation,
parser and sound compatibility, and SCI1.1 graphics encoding still require work
before a complete SCI0 game can run as SCI1.1. Said and synonym payloads are
rejected rather than discarded. No real-game playback proof is claimed.

Synthetic checks are registered as `sci11_script_tests`. Builds, tests and ScummVM
playback remain manual; they were not run during implementation.

Layout facts were cross-checked against sci-tools `SCI/Resource/Script11.cs`,
ScummVM `engines/sci/engine/script.cpp`, and SCI Companion's script serializer.
The implementation is independent and contains no private game code or assets.
