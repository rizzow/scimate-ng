# One-picture VGA conversion

`write_sci1_vga_picture` writes a full 320x190 SCI1 VGA picture from decoded
visual, priority and control planes plus a 256-entry RGB palette. It emits an
embedded bitmap with SCI1 RLE and vector masks. `ega_picture_palette` preserves
existing EGA colours while allowing later edits to use additional palette slots.
Visual index 255 is reserved for transparency; priority/control remain 4-bit.
Output is bounded by the SCI1 resource-size limit.

Decoded pictures now retain separate drawing masks: a white visual pixel or zero
priority/control pixel can be an explicit write, not just an untouched default.
`VgaPictureMode::overlay` emits only masked vector writes, including explicit
zero values. It avoids embedded-cel priority filtering and does not overwrite
untouched pixels. Callers must provide drawing masks for every intended write.
Pictures with flood fills are refused in this mode because their result can
depend on the existing background. The default `background` mode remains a
full-room bitmap conversion. Neither this mode choice nor mirrored EGA dithering
is yet handled by the migration's DrawPic rewrite.

These limits follow [ScummVM's picture drawing code](https://github.com/scummvm/scummvm/blob/master/engines/sci/graphics/picture.cpp):
embedded cels test the existing priority plane, while vector flood fills inspect
existing screen contents. A flattened bitmap is therefore not a generic overlay
replacement.

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
