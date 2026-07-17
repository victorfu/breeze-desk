# Importing media

Supported inputs are WAV, MP3, M4A, AAC, FLAC, OGG, Opus, MP4, MOV, MKV, and WebM. Audio is extracted
from video locally. File dialogs, drag-and-drop, folder import, Unicode filenames, spaces, and multiple
selected files use native paths without constructing a shell command.

## Import modes

- **Reference original** records the source path and leaves the media where it is. Moving the file later
  requires **Relink Source**.
- **Copy into managed storage** writes a hash-named copy under BreezeDesk's application-data directory
  before adding the recording. The original remains untouched.

Choose the default under **Settings > General > Import behavior**. A folder scan reports how many files
have been found and copied and can be cancelled. Duplicate titles receive a safe distinct title; existing
source records and unsupported entries are reported rather than silently replacing library data.

## What happens before transcription

1. FFprobe inspects streams, sample rate, channel count, and duration.
2. Available disk space is checked before conversion.
3. The packaged FFmpeg writes 16 kHz, mono, signed PCM16 WAV to a temporary cache path.
4. BreezeDesk validates the RIFF/WAVE structure, PCM format, output size, and duration tolerance.
5. The file is atomically committed and multiresolution waveform peaks are generated.

Cancelling or a conversion failure removes the temporary output and preserves any previously valid
cache. The source is never rewritten. Normalization progress appears in Queue and cannot block playback
or navigation.

## Missing and removed media

A missing-source badge means the referenced file no longer exists. Open the card menu, choose **Relink
Source**, and select a supported replacement. Relinking clears source-dependent cache metadata so the new
file is inspected again. **Reveal** opens the containing Finder or Explorer location when it still exists.

Moving a recording to Trash is reversible. Permanent delete removes the transcript, managed copy, and
cache after confirmation, but deliberately refuses to delete the original source path. See
[Transcription](transcription.md) for the next stage and [Troubleshooting](troubleshooting.md) when
FFmpeg or disk validation fails.
