# Source provenance and licensing

Reference: local `../SCICompanion`, revision
`f1a603b48b1aa7abf94f78574a8f69a653e2ca62`.
The original repository is used read-only.

The SCI0 parsing implementation in `core/src/resource_map.cpp` adapts format
and navigation behavior from the following Philip Fortier sources, which
declare copyright (c) 2015 and GPL version 2 or later:

- `SCICompanionLib/Src/Resources/ResourceBlob.h`: `LAYOUT_SCI0`,
  `RESOURCEMAPENTRY_SCI0_BASE`, and all-ones terminator documentation.
- `SCICompanionLib/Src/Resources/ResourceContainer.h`: `SCI0MapNavigator`,
  sequential records and six-byte terminator emission.
- `SCICompanionLib/Src/Util/sci.h`: resource type numbers and names.

Also inspected `ResourceSources.h` for the map/volume boundary and
`ResourceMapOperations.cpp` for explicit format dispatch (both in the same
Resources directory). No Windows I/O, packed structs, UI code, or third-party
codec implementations were imported. Native bitfields were replaced by bounded
byte reads, fixed-width integers, shifts and masks. Strict malformed-input
checks and the new CLI/tests are additions.

The original copyright/license notice is retained in the adapted source.
`COPYING` is copied verbatim from `SCICompanion/Files/COPYING2`. New source files
use SPDX GPL-2.0-or-later identifiers for compatibility with this adaptation;
this does not assign a license to future third-party imports or game assets.

Committed fixtures are synthetic byte sequences in the tests. The local SCI0
template game is an additional validation input only; none of its assets are
copied into this repository. No behavioral comparison against the running
Windows editor has been performed.
