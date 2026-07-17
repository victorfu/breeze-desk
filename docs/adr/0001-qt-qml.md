# ADR 0001: Qt and QML desktop architecture

Status: Accepted — 2026-07-17

## Context

The application needs native multimedia, local IPC, SQLite, long-running background work, high-DPI and
accessibility support on macOS and Windows without Electron, Tauri, Python, or a browser runtime. The UI
must remain responsive with ten thousand Library rows and thousands of transcript segments.

## Decision

Use C++17, Qt 6.8+, Qt Quick, and Qt Quick Controls 2. QML is a declarative presentation layer over
injected C++ ViewModels and `QAbstractListModel` implementations. SQL, settings, platform decisions,
filesystem policy, media processes, and business rules stay in static C++ libraries. QML uses semantic or
component design tokens rather than primitive colors/sizes directly.

Qt is dynamically deployed under LGPL terms. Widgets are limited to desktop integration such as
`QApplication` and tray support; application screens remain QML.

## Consequences

- Domain targets remain testable with Qt Test without loading QML.
- ListView reuse and a C++ scene-graph waveform item bound UI object count and rendering work.
- The composition root must register every ViewModel/singleton and keep QObject ownership explicit.
- Every visible string must use Qt translation and every command needs keyboard/accessibility metadata.
- Packaging must deploy replaceable Qt libraries, QML imports, plugins, and license material.

## Rejected alternatives

Electron/Tauri add a web runtime and conflict with the requested native toolchain. QWidget screens would
split the design system and make the required QML architecture impossible. Putting business logic in QML
JavaScript would weaken type safety, testing, threading, and persistence boundaries.
