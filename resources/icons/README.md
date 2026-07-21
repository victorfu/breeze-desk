# BreezeDesk brand icon assets

The BreezeDesk app mark is the selected Codex GPT-Image2 infinity loop with a swept breeze tail. The
user-selected artwork is preserved across the raster asset family after chroma-key removal. Brand
icons must remain PNG/ICO/ICNS; do not create an SVG substitute.

## Palette

- Representative blue: `#406DE5` (the app tile retains the selected image's blue gradient)
- Symbol: warm white with the selected artwork's original shading
- Breeze accent: translucent pale blue from the selected artwork

## Checked-in sources

- `breezedesk.png` is the canonical 1024 x 1024 RGBA app icon used by documentation, application
  windows, and macOS app-icon packaging.
- `breezedesk-sidebar.png` is the 512 x 512 app/sidebar rendering.
- `breezedesk-tray.png` is the 256 x 256 Windows tray source. It preserves the selected swept tail and
  is visually checked at 16-64 px before release.
- `breezedesk-menubar-Template.png` and `breezedesk-menubar-Template@2x.png` are the adaptive black
  macOS menu-bar glyphs at 18 x 18 and 36 x 36.
- `breezedesk-unplated.png` and `breezedesk-light-unplated.png` are transparent white and blue
  infinity glyphs used by the Windows shell on dark and light surfaces.
- `breezedesk.ico` contains native 32-bit RGBA Windows frames at 16, 20, 24, 28, 30, 32, 36, 40,
  48, 56, 60, 64, 72, 80, 96, 128, and 256 px.
- `breezedesk.icns` is the checked-in macOS bundle icon used by ordinary CMake builds.

## Packaging outputs

Windows release packaging regenerates the ICO from the full and small-size PNG sources:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File packaging\windows\generate-icon.ps1 `
  -OutputFile resources\icons\breezedesk.ico
```

MSIX packaging produces the complete Square44 target-size set (including light/dark unplated
variants), scale-qualified Square44, Square150, StoreLogo, and Wide310 assets, then indexes all
qualified resources into `resources.pri`.

macOS packaging derives the standard 16, 32, 128, 256, and 512 pt iconset with every `@2x` partner
from the 1024 px canonical PNG, then rebuilds `breezedesk.icns` with `iconutil`.

Keep the selected infinity silhouette, center crossing, optical padding, and swept breeze tail
consistent. Re-check the small-size and menu-bar previews whenever the canonical artwork changes.
