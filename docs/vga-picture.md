# One-picture VGA conversion

`write_sci1_vga_picture` writes a full 320x190 SCI1 VGA picture from decoded
visual, priority and control planes plus a 256-entry RGB palette. It emits an
embedded bitmap with SCI1 RLE and vector masks. `ega_picture_palette` preserves
existing EGA colours while allowing later edits to use additional palette slots.
Visual index 255 is reserved for transparency; priority/control remain 4-bit.
Output is bounded by the SCI1 resource-size limit.

A private conversion of picture 200 is stored under the ignored
`migration-output/pic200-vga/` directory. It contains a decoded payload, a
ScummVM picture patch, and a preview. No private resource or artwork is tracked.
The first conversion preserves the original artwork rather than inventing new
graphics. Playback is unverified and remains manual.

ScummVM selects EGA versus VGA vector-picture interpretation using the game's
graphics mode. A standalone VGA picture cannot be installed in the current
SCI1 EGA export. Running the whole game in VGA requires a consistent graphics
profile and compatible parser behavior; ScummVM's generic SCI1 VGA path disables
its native parser. A portable SCI-script parser is the route to preserving typed
commands without requiring ScummVM changes or game-identity exceptions.

Format facts were checked against upstream ScummVM graphics/picture.cpp,
graphics/view.cpp and graphics/palette16.cpp. The encoder is independently
implemented; no ScummVM code or private game assets are copied into the source.
