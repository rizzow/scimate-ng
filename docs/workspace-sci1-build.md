# Build an edited workspace for SCI1

Export the original SCI0 game using the existing workspace command:

```sh
./build/scimate export ~/Games/LSL3 ~/Games/LSL3-workspace
```

Find picture 200's row in `manifest.tsv`: type `1`, number `200`. Its `index`
identifies the zero-padded bundle under `pictures/` (for example, index 42 uses
`pictures/0000042/`). The directory contains `visual.png`, `priority.png`,
`control.png`, `picture.tsv`, and `original.bin`.

Edit `visual.png` as an opaque 320×190 image. A VGA build preserves the original
16 EGA palette entries and accepts up to 239 additional RGB colours, assigned
indices 16–254. Index 255 remains reserved for transparency. Images exceeding
that palette capacity fail explicitly; the importer does not quantize artwork.
Priority and control PNGs must use exact EGA colours representing indices 0–15.
Keep the manifest, original snapshot, and picture metadata unchanged.

Build the edited workspace with:

```sh
./build/scimate import --target-engine sci1 --graphics vga \
  ~/Games/LSL3-workspace ~/Games/LSL3-vga
```

Add `--force` to replace an existing output directory. The CLI generates the
candidate before replacement and rejects source/destination overlap. VGA
picture edits bypass SCI0 vector re-encoding and enter the VGA encoder directly.
Other resources retain the existing workspace import and conflict checks.
Conflicting raw picture payload and PNG edits fail rather than selecting one.
Oversized pictures use bounded continuation resources through the DrawPic bridge.

Directory exports now preserve ordinary auxiliary source files in `original/`.
The SCI1 builder copies referenced data from that snapshot. Older workspaces
may lack these files; unresolved references appear in `migration-report.txt`.

Without `--target-engine`, import continues to rebuild SCI0. SCI1 defaults to EGA
unless `--graphics vga` is supplied. The SCI1 converter and portable parser remain
experimental; a successful build does not prove gameplay compatibility.
