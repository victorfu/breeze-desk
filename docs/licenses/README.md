# Packaged license material

`THIRD_PARTY_NOTICES.md` records repository-level dependency attribution. Release packaging additionally
copies the exact upstream Sparkle or WinSparkle license files, the FFmpeg source/build configuration,
and the applicable MinGW-w64 runtime notice beside each binary distribution. `Qt-LGPL-NOTICE.md`, the
complete LGPL/GPL terms, and a generated
`Qt-SOURCE.txt` record for the exact configured Qt version are copied into every package. Qt remains
dynamically linked and replaceable under its LGPL terms.

The complete Lucide/Feather license and immutable source record live under
`resources/icons/lucide/` and are copied unchanged into every package that contains those SVG assets.

Dependency-version-specific source records are generated during CMake configure so they match the
libraries shipped by the packaging scripts.
