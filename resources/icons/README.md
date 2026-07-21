# BreezeDesk brand icon assets

The BreezeDesk app mark is an original infinity loop with an integrated breeze cut, generated with
Codex GPT-Image2 and finished as raster assets. Brand icons must remain PNG/ICO/ICNS; do not create an
SVG substitute.

## Palette

- Primary blue: `#4B70E2`
- Breeze accent: `#93ACF5`
- Symbol: `#FFFFFF`

## Checked-in sources

- `breezedesk.png` is the canonical 1024 x 1024 RGBA app icon used by documentation, application
  windows, and macOS app-icon packaging.
- `breezedesk-sidebar.png` is the 512 x 512 app/sidebar rendering.
- `breezedesk-tray.png` is the optically simplified 256 x 256 Windows tray source. Its breeze accent
  becomes negative space so the mark remains legible at 16-48 px.
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

Keep the infinity silhouette, center crossing, optical padding, and integrated breeze cut consistent.
Do not replace the small-size or menu-bar assets with a mechanical downscale of the large icon.
