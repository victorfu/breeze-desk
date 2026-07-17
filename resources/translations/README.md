# Translation resources

Editable TS catalogs live in the repository-level `translations/` directory. CMake generates and embeds
the QM catalogs from that single source; packaged applications do not load arbitrary translations from
this directory.
