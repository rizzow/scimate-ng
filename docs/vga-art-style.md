# Tropical VGA art direction

Use this guide and the approved terrace image as references when recreating
rooms. The reference image establishes rendering style; each original room
establishes geometry. Never transfer scenery from one room into another.

## Format and geometry

- Native artwork: 320×190, opaque PNG; no borders or UI painted into the scene.
- Preserve paths, exits, object footprints, horizon, foreground silhouettes,
  camera perspective and interaction landmarks. Keep priority/control maps.
- Draw for the game's aspect-corrected display, where pixels appear taller.
- Use coherent colour clusters and hand-painted pixel shading. No EGA
  checkerboards, ordered dithering, grain or photographic detail.
- Preserve EGA indices 0–15 for legacy sprites/UI. Imported artwork may use
  239 additional RGB colours; index 255 is reserved for transparency.

## Colour families

These RGB swatches are art-direction anchors, not mandatory palette indices.
Shade and interpolate within each family; do not mechanically recolour every
room. The importer currently allocates additional palette entries per picture.

| Material | Deep shadow | Midtone | Light | Highlight |
|---|---|---|---|---|
| Tropical foliage | `#082A21` | `#236B31` | `#63A83D` | `#B1CE69` |
| Cool jungle shade | `#071A22` | `#123E42` | `#28605A` | `#5B8C76` |
| Palm trunks and timber | `#241B16` | `#62442D` | `#A47443` | `#CFA76C` |
| Soil and leaf litter | `#36291A` | `#7E5B2D` | `#B68A45` | `#D3AF71` |
| Stone and concrete | `#424C51` | `#7C8582` | `#ABB0A2` | `#D5D3BC` |
| Teal painted metal | `#123D47` | `#267A86` | `#60ABB2` | `#A1D1CC` |
| Ocean and fresh water | `#123C76` | `#176EAA` | `#36B7C4` | `#A3E2DA` |
| Sky | `#295DAD` | `#4D8BD5` | `#95C3E7` | `#DBE7DC` |

## Light and texture

Daylight is warm and clear, with cooler blue-green shadows. Match the source
room's light direction. Outdoor terraces can be bright; covered jungle rooms
must stay darker with selective light on the path, foliage and water.

Foliage uses layered leaf masses with a few clear fronds, not equal-detail
noise everywhere. Rock has broad facets and restrained cracks. Water uses
horizontal highlights or coherent falling streams. Metal has thin edge lights.
Keep readable dark foreground silhouettes without flattening every shadow to
pure black. Saturated colours belong mainly to water, selected vegetation and
small existing accent objects; avoid neon green across the whole scene.

## Reusable generation prompt

"Recreate the target room as early-1990s SCI1 VGA painted pixel art. Preserve
its exact composition, walkable boundaries, foreground occlusion silhouettes
and interaction landmarks at 320×190. Use the approved terrace only as a colour
and rendering-style reference. Follow the tropical VGA colour families, warm
light and cool shadows, adapted to this room's lighting. Replace EGA dithering
with coherent shaded colour clusters. No added objects, characters, landmarks,
text, borders, checkerboards or grain."

## Review before importing

Check geometry against the source, colour/texture treatment against an approved
room, and the final indexed image after reduction. Verify that any path changes
remain consistent with existing priority/control maps. Keep the original PNG
as `visual-ega.png` when installing the selected VGA artwork as `visual.png`.
Use a temporary sibling `visual-vga.png` during review; never overwrite an
existing EGA backup. See [the AI artwork workflow](ai-vga-artwork.md).
