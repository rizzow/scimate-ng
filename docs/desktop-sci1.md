# SCI1 and VGA in the desktop

The editing project remains an SCI0 source workspace. Select
**Project → Preview VGA Workspace Edits** before opening RGB artwork, or toggle it
on an open project to reload. The setting is remembered for subsequent opens.
Validated RGB pictures and view edits are retained separately from SCI0 decoded
payloads. The resource browser, animation preview and room explorer display their
palettes; the effective edited resources are marked Modified. Game Info reports
the source format and override counts. If mode switching fails, the previous
validated project stays open; its Game Info reflects the successfully loaded mode.

Pictures accept opaque 320×190 RGB visual PNGs; priority/control masks retain exact
EGA colours for indices 0..15. Views accept RGB/RGBA/indexed cel PNGs with binary
alpha, either in the original bundle or its complete `vga/` sibling set. Each
palette reserves the original 16 EGA colours and accepts up to 239 additional
colours. No automatic quantization is performed. Follow the existing
[artwork workflow](vga-migration.md) for metadata and conflict rules.

**Build Resources to New Folder** offers:

- **SCI0 EGA:** the existing SCI0 reconstruction, requiring EGA-compatible edits.
- **SCI1 EGA:** experimental conversion while retaining EGA graphics.
- **SCI1 VGA:** experimental conversion using RGB picture/view overrides.

Every build re-reads current disk edits. Output folders must be new; the GUI does
not offer destructive replacement. SCI1 builds run the same converter as CLI
`import --target-engine sci1 --graphics ega|vga`, and display `migration-report.txt`
on success. Review unsupported or unresolved items and validate the generated game
in ScummVM. The output is resources and referenced auxiliary data, not an installed
interpreter or proof of playable gameplay.

**File → Inspect SCI1 / SCI1.1 Game…** opens an independent read-only inspector.
Choose the map format explicitly: packed SCI1 EGA/VGA, SCI1 late, or SCI1.1. It
decodes map/volume records asynchronously, offers resource filtering, and exports
decoded binary payloads. Supported SCI1 pictures and view cels can be previewed
and exported as PNG. Graphics decoding covers the implemented SCI1 subset;
SCI1.1 graphics and views requiring a shared external palette remain binary-only.
Unsupported previews display a diagnostic while keeping binary inspection usable.

Script explanations and room discovery still inspect the SCI0 source. Initial
sprite composition remains approximate. PNG exports reflect the displayed image;
decoded binary exports retain the source payload, including when VGA overrides
are displayed. No migrated payload is substituted into the source project.
