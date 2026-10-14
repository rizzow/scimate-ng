# Reusable EGA-to-VGA artwork prompt

Paste the following into a separate thread and replace `RESOURCE_PATH`.

---

Recreate this EGA resource as consistent SCI1 VGA artwork:

**Resource:** `RESOURCE_PATH`

Read and follow the complete art-direction guide at `docs/vga-art-style.md`
and the workflow at `docs/ai-vga-artwork.md` in this repository.
Use an attached approved VGA image as the rendering/colour reference
(for the tropical examples, the approved terrace). It supplies style only;
the supplied resource supplies the scene, subject and geometry.

Inspect the resource before generating. Accept a picture/view PNG, an exported
picture/view bundle, or a decoded SCI0 pic/view payload. For binary resources,
use the project's existing export functionality to obtain viewable images and
metadata. Distinguish decoded payloads from patch files and packed game volumes;
do not guess their format or send binary data directly to image generation.
Use the imagegen skill and built-in image generation for the actual recreation.

For a **picture**, preserve its exact framing, paths, exits, camera perspective,
object footprints and foreground occlusion silhouettes. Output an opaque
320×190 `visual-vga.png`, with coherent VGA shading and no EGA dithering.
Leave priority/control PNGs and original snapshots untouched. Keep the first
16 EGA colours available to existing sprites/UI: reduce the generated artwork
without dithering to at most 239 colours, compatible with the current SCI1
workspace importer. After reviewing the prepared image, rename the original
`visual.png` to `visual-ega.png` and install the VGA version as `visual.png`.
Preserve any existing `visual-ega.png`; use it as the EGA target for later
iterations. The importer reads `visual.png`, not the temporary review sibling.

For a **view**, inspect all loops/cels and mirror relationships first. Preserve
each cel's dimensions, silhouette, placement, transparent background and
animation identity. Maintain consistent character/material colours and lighting
across every frame. Recreate actual cels; preserve mirrored-loop metadata instead
of generating unrelated mirror frames. Save transparent sibling PNGs with
matching loop/cel filenames in a separate `vga/` directory, plus unchanged
placement/mirror metadata. Do not add scenery, shadows outside the silhouette,
cropping or padding that changes registration. Check alpha and edge pixels after
palette reduction. Keep a complete source-cel set and copy the unchanged parent
`view.tsv` into `vga/`. SCI1 VGA workspace import reads these siblings with one
embedded palette per view: reserve EGA colours 0–15 and transparency index 255,
and use at most 239 additional RGB colours across all cels. Alpha must be binary;
reduce the whole animation without dithering before import. Do not edit the
parent EGA PNGs or raw payload at the same time as a VGA override. SCI0 import
still requires EGA colours and ignores `vga/`. Source support does not establish
game readiness: report whether encoding and interpreter playback were verified.

Prefer source-faithful geometry and game consistency over extra detail. Use
the guide's colour families with room-appropriate lighting. No invented objects,
characters, text, borders, stippling, grain or photographic rendering.

Inspect the final files and verify dimensions, palette size and transparency.
Show the result inline, report exact saved paths, and provide the applicable
manual import command. Preserve originals and keep commercial game assets out
of Git. Do not run builds, tests, Python scripts, or whole-game export/import
commands. Resource-only exports and image preparation are authorised for this
task. Use clangd for source navigation if needed; avoid repeated Git inspection.

---
