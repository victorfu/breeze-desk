# Contributing

Contributions are welcome through focused issues and pull requests. Build with Qt 6.10.1 when
possible, keep C++ at C++17, run `scripts/run-tests.*`, and run clang-format before submitting.

Keep the target dependency graph acyclic: domain code must not depend on QML, SQL must remain behind
repositories, settings access must remain in subsystem managers, and platform conditions must remain
in the platform target. Production code may not introduce cloud ASR, telemetry, `whisper-cli`, Python
runtime dependencies, placeholder actions, or unpinned dependencies.

Tests use Qt Test and live under `tests/<Subsystem>/tst_<Component>.cpp`. Ordinary tests must not
download large models. Include a migration, failure-path test, documentation, and license notice when
a change affects persistent data or adds a dependency.

By contributing, you agree that your contribution is licensed under the repository MIT license.
