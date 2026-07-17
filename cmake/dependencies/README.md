# Dependency integration

Third-party targets are pinned and configured from the root `CMakeLists.txt`. Keep future dependency
adapters in this directory; never introduce an unpinned branch or an implicit global include path.
