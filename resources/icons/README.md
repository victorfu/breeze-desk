# BreezeDesk icon assets

- `breezedesk-sidebar.png` is the canonical app icon. The in-app logo, window, taskbar, tray,
  executable, installer, macOS bundle, and Windows package assets must all be derived from it.
- `breezedesk.png` is the detailed, large-format artwork used only for documentation and marketing.
- `breezedesk-menubar.png` remains the macOS menu-bar asset.
- `breezedesk.ico` contains native 16, 20, 24, 28, 32, 40, 48, 56, 64, 80, 96, 128,
  and 256 px Windows frames.

Regenerate the checked-in Windows ICO after changing the canonical app icon:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File packaging\windows\generate-icon.ps1 `
  -OutputFile resources\icons\breezedesk.ico
```

Do not substitute a simplified symbol for small sizes; Windows must show the same complete otter mark
that appears in the app sidebar.
