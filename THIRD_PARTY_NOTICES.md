# Third-Party Notices

## SnapTray

Architecture and selected general-purpose patterns were studied and adapted from SnapTray at commit
`1dd57a401d5b278b5f55b247a533c07027cb8a9d`:
https://github.com/victorfu/snap-tray

Copyright (c) 2026 Victor Fu

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and
associated documentation files (the "Software"), to deal in the Software without restriction,
including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense,
and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so,
subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial
portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT
LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN
NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

## whisper.cpp

Pinned to commit `f049fff95a089aa9969deb009cdd4892b3e74916` (v1.9.1).
Copyright (c) 2023-2026 The ggml authors. MIT License.
https://github.com/ggml-org/whisper.cpp

Release packages include the pinned checkout's complete `LICENSE` as `whisper.cpp-MIT.txt`.

## Qt

Qt 6 is dynamically linked and redistributed under the GNU Lesser General Public License v3. Users
may replace the deployed Qt libraries. Complete license terms and a matching-version source record are
included in binary packages. Corresponding Qt source and license information are also available
from https://www.qt.io/licensing/open-source-lgpl-obligations

## FFmpeg

Release media tools are built from FFmpeg 8.1.2 source archive SHA-256
`464beb5e7bf0c311e68b45ae2f04e9cc2af88851abb4082231742a74d97b524c`, with GPL and nonfree components
disabled. The exact `-buildconf` output and source record ship beside release notices. FFmpeg is covered
by LGPL 2.1 or later. https://ffmpeg.org/legal.html

Windows sidecars are built with w64devkit 2.8.0. The complete MinGW-w64 runtime notice supplied by
that toolchain is distributed as `MinGW-w64-runtime.txt` beside the FFmpeg notices.
https://github.com/skeeto/w64devkit

## Sparkle

Direct-download macOS packages use Sparkle 2.9.2 at revision
`6276ba2b404829d139c45ff98427cf90e2efc59b`. Sparkle and its included notices are distributed under
permissive licenses; the complete upstream `LICENSE` file is copied into the application bundle.
https://github.com/sparkle-project/Sparkle

## WinSparkle

Direct-download Windows packages use WinSparkle 0.9.3 at revision
`8ca58d903779b866eb9ed4628b0a36e4d488b623`. Its MIT notice, Expat notice, and upstream acknowledgements
are copied into the installed license directory. https://github.com/vslavik/winsparkle

## Breeze-ASR-25 models

Models are downloaded separately and are not part of the application distribution. The MediaTek base
repository and community whisper.cpp conversions declare Apache License 2.0. Exact artifact revisions,
sizes, checksums, source links, and license links are stored in `resources/models/models.json`. The app
describes the conversion as community-produced and does not claim it is an official MediaTek GGML build.

## Silero VAD model

`ggml-silero-v6.2.0.bin` is downloaded separately from ggml-org/whisper-vad revision
`9ffd54a1e1ee413ddf265af9913beaf518d1639b` under the MIT License.

## Lucide icons

The UI vendors a selected set of SVG symbols from Lucide 1.16.0 at immutable commit
`2214caa407f4147449c81ac27e30d36edfb7b40f`. Lucide is distributed under the ISC License and some
symbols derive from Feather under MIT. The complete unchanged upstream `LICENSE` and source record ship
with the application. https://github.com/lucide-icons/lucide

No third-party model or gigabyte-scale asset is included in this repository or an installer.
