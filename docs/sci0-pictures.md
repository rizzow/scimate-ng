# Editable SCI0 backgrounds and masks

Game exports include `pictures/NNNNNNN/` bundles for SCI0 EGA picture resources.
The ordinal matches `payloads/NNNNNNN.bin`, so duplicate IDs remain separate.
Each bundle contains `visual.png`, `priority.png`, `control.png`, `picture.tsv`,
and the decoded picture snapshot `original.bin`.

Edit the PNGs, then run `scimate import` on the game export. The visual image
is the dithered EGA background. Priority indices control sprite occlusion;
control indices identify collision/region masks. Mask PNG colors represent the
same EGA indices 0–15, not grayscale intensities. All planes are opaque,
320×190 pixels, with top-to-bottom rows. PNGs may be indexed, RGB, or RGBA,
but colors must exactly match EGA and alpha must be fully opaque. Resizing,
quantization, antialiasing, and transparency are rejected. Keep the manifest
and snapshot unchanged.

Standalone decoded picture payloads use:

```sh
scimate picture-export picture.bin new-picture-folder
scimate picture-import new-picture-folder edited-picture.bin
```

Unchanged imports return the original vector bytes exactly. Edits append
horizontal vector strokes before the original terminator, retaining the
original drawing commands and trailing bytes. Each plane is edited independently,
including white visual pixels and zero mask indices. Appended visual strokes
set explicit EGA palettes so their colors do not depend on the runtime palette.
The edited payload is rendered again and all three planes are checked before
writing. This is pixel editing of the final picture, not a vector editor;
overlay use and runtime palette effects may differ after visual edits.

Full-game import verifies snapshots and rejects conflicting raw-payload/PNG
edits, just as for views. A raw edit alone remains valid when the PNG bundle
is unchanged. Commands never overwrite existing output destinations.

Supported drawing includes absolute, short and medium lines; fills; solid and
textured circle/rectangle pens; EGA palette updates; and monochrome metadata.
Coordinates outside the canvas are clipped. Embedded bitmap extensions and
SCI1 priority-table extensions are unsupported and get `.error.txt` sidecars
on export, retaining their raw payloads. Rendering is bounded to 64 million
pixel writes per resource. SCI0 payloads cannot exceed 65,535 bytes (packaged
stored records have the existing stricter 65,531-byte limit); highly detailed
edits can exceed this limit and fail explicitly. Engine behavior is not verified.
