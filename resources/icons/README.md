# BreezeDesk icon assets

- `breezedesk.png` is the detailed, large-format artwork used for marketing and large tiles.
- `breezedesk-symbol.svg` is the flat brand mark used by the window and the 32 px in-app logo.
- `breezedesk-tray.svg` is optically simplified for 16–28 px Windows system-tray sizes.
- `breezedesk-menubar.png` remains the macOS menu-bar asset.
- `breezedesk.ico` contains native 16, 20, 24, 28, 32, 40, 48, 56, 64, 80, 96, 128,
  and 256 px Windows frames.

Regenerate the checked-in Windows ICO after changing either SVG:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File packaging\windows\generate-icon.ps1 `
  -OutputFile resources\icons\breezedesk.ico
```

Keep the small assets flat, high-contrast, and free of texture or sub-pixel strokes. Do not derive tray
frames from the detailed PNG.
