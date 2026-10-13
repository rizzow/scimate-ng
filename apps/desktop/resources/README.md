# Desktop icon

`scimate.png` is the source artwork used by Qt's application/window icon.
`scimate.icns` contains macOS sizes from 16 px through 1024 px for Finder and Dock.
Both are committed; normal builds do not need image-generation or conversion tools.

Created with the built-in imagegen tool. Final prompt:

> Use case: logo-brand. Create one finished macOS desktop application icon for SCIMate, an editor and explorer for classic SCI adventure game resources. Square 1024x1024 canvas. A single large midnight-blue rounded-square app tile, centered with a modest transparent outer margin, straight-on. Center a bold, simple pixel-art open doorway with a warm amber opening, cyan stone arch and a tiny pixel cursor suggesting editing, using a restrained classic EGA-like palette. The doorway is the dominant easily recognizable silhouette, occupying most of the tile. Crisp pixel geometry for the emblem, tasteful subtle depth on the tile, premium restrained desktop icon finish. Readable at 32 pixels, not a detailed scene. No text, no letters, no watermark, no grid, no extra objects, no mockup or device. Actual transparent background outside the tile.

To regenerate the ICNS from the source PNG on macOS:

```sh
mkdir -p build-icon/scimate.iconset
for size in 16 32 128 256 512; do
  sips -z "$size" "$size" apps/desktop/resources/scimate.png \
    --out "build-icon/scimate.iconset/icon_${size}x${size}.png"
  retina=$((size * 2))
  sips -z "$retina" "$retina" apps/desktop/resources/scimate.png \
    --out "build-icon/scimate.iconset/icon_${size}x${size}@2x.png"
done
iconutil -c icns build-icon/scimate.iconset -o apps/desktop/resources/scimate.icns
```
