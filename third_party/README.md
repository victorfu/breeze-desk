# Third-party source policy

BreezeDesk does not vendor buildable dependency source in this directory. CMake fetches whisper.cpp at
the immutable revision declared by `BREEZEDESK_WHISPER_CPP_REF`, while packaging scripts consume pinned
sidecars and preserve their licenses. This directory records any future vendored compatibility patches.
